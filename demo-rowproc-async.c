/*
 * Row processor async demo.
 *
 * usage: rowproc-async [connstr [query]]
 */


#include <sys/select.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libpq-fe.h>

struct Context {
	PGconn *db;
	int count;
};

/* print db error message and die */
static void die(PGconn *db, const char *msg)
{
	fprintf(stderr, "%s: %s", msg, PQerrorMessage(db));
	exit(1);
}

/* wait for event on socket */
static void db_wait(PGconn *db, int for_write)
{
	int fd = PQsocket(db);
	fd_set fds;
	int res;

retry:
	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	if (for_write)
		res = select(fd+1, NULL, &fds, NULL, NULL);
	else
		res = select(fd+1, &fds, NULL, NULL, NULL);

	if (res == 0)
		goto retry;
	if (res < 0 && errno == EINTR)
		goto retry;
	if (res < 0)
	{
		fprintf(stderr, "select() failed: %s", strerror(errno));
		exit(1);
	}
}

/* do something with one row */
static void proc_row(struct Context *ctx, PGresult *res, const PGdataValue *columns)
{
	ctx->count++;

	if (0)
	printf("column: %.*s\n",
		   columns[0].len,
		   columns[0].value);
}

/* do something with resultset final status */
static void proc_result(struct Context *ctx, PGresult *r)
{
	ExecStatusType s;

	s = PQresultStatus(r);
	if (s == PGRES_TUPLES_OK)
		printf("query successful, got %d rows\n", ctx->count);
	else
		printf("%s: %s\n", PQresStatus(s), PQerrorMessage(ctx->db));
	PQclear(r);
}

/* custom callback */
static int my_handler(PGresult *res, const PGdataValue *columns, const char **errmsgp, void *arg)
{
	struct Context *ctx = arg;

	if (!columns)
		return 1;

	proc_row(ctx, res, columns);

	return 1;
}

/* this handles socket read event */
static int socket_read_cb(struct Context *ctx, PGconn *db)
{
	PGresult *r;

	/* read incoming data */
	if (!PQconsumeInput(db))
		return -1;

	/*
	 * one query may result in several PGresult's,
	 * wrap everything in one big loop.
	 */
	while (1) {
		/* need to wait for more data from network */
		if (PQisBusy(db))
			return 0;

		/* we have complete PGresult ready */
		r = PQgetResult(db);
		if (r == NULL) {
			/* all results have arrived */
			return 1;
		} else {
			proc_result(ctx, r);
		}
	}
}

/* run query with custom callback */
static void exec_query(struct Context *ctx, PGconn *db, const char *q)
{
	int res;
	int waitWrite;

	ctx->count = 0;

	/* set up socket */
	PQsetnonblocking(db, 1);

	PQsetRowProcessor(db, my_handler, ctx);

	/* launch query */
	if (!PQsendQuery(db, q))
		die(db, "PQsendQuery");

	/* see if it is sent */
	res = PQflush(db); // -1:err, 0:ok, 1:more
	if (res < 0)
		die(db, "flush 1");
	waitWrite = res > 0;

	/* read data */
	while (1) {
		db_wait(db, waitWrite);

		/* got event, process it */
		if (waitWrite) {
			res = PQflush(db); // -1:err, 0:ok, 1:more
			if (res < 0)
				die(db, "flush 2");
			waitWrite = res > 0;
		} else {
			res = socket_read_cb(ctx, db);
			if (res < 0)
				die(db, "socket_read_cb");
			if (res > 0)
				return;
			waitWrite = 0;
		}
	}

	PQsetRowProcessor(ctx->db, NULL, NULL);
}

int main(int argc, char *argv[])
{
	const char *connstr;
	const char *q;
	PGconn *db;
	struct Context ctx;

	connstr = "dbname=postgres";
	if (argc > 1)
		connstr = argv[1];

	q = "show all";
	if (argc > 2)
		q = argv[2];

	db = PQconnectdb(connstr);
	if (!db || PQstatus(db) == CONNECTION_BAD)
		die(db, "connect");
	ctx.db = db;

	exec_query(&ctx, db, q);

	PQfinish(db);

	return 0;
}

