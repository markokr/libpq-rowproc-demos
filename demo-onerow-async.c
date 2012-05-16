/*
 * PQsetSingleRowMode async demo.
 *
 * usage: ./onerow-async [connstr [query]]
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

static void die(PGconn *db, const char *msg)
{
	if (db)
		fprintf(stderr, "%s: %s", msg, PQerrorMessage(db));
	else
		fprintf(stderr, "%s", msg);
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

static void proc_row(struct Context *ctx, PGresult *res)
{
	const char *val = PQgetvalue(res, 0, 0);
	ctx->count++;
	if (0)
	printf("column#0: %s\n", val ? val : "NULL");
}

static void proc_result(struct Context *ctx, PGresult *r)
{
	ExecStatusType s;

	s = PQresultStatus(r);
	if (s == PGRES_TUPLES_OK)
		printf("query successful, got %d rows\n", ctx->count);
	else
		printf("%s: %s\n", PQresStatus(s), PQerrorMessage(ctx->db));
}

/*
 * Handle socket read event
 *
 * Returns:
 * -1 - error
 *  0 - need to read more data
 *  1 - all done
 */

static int socket_read_cb(struct Context *ctx)
{
	PGresult *r;
	ExecStatusType s;

	/* read incoming data */
	if (!PQconsumeInput(ctx->db))
		return -1;

	/*
	 * One query may result in several PGresults,
	 * first loop is over all PGresults.
	 */
	while (1) {
		/* Need more data from network? */
		if (PQisBusy(ctx->db))
			return 0;

		/* Get next result */
		r = PQgetResult(ctx->db);
		if (!r)
			/* all results have arrived */
			return 1;

		s = PQresultStatus(r);
		if (s == PGRES_SINGLE_TUPLE) {
			/* process row tuple in incomplete result */
			proc_row(ctx, r);
		} else {
			/* process final resultset status */
			proc_result(ctx, r);
		}
		PQclear(r);
	}
}

static void exec_query(struct Context *ctx, const char *q)
{
	int res;
	int waitWrite;
	PGconn *db = ctx->db;

	ctx->count = 0;

	/* launch query */
	if (!PQsendQuery(ctx->db, q))
		die(ctx->db, "PQsendQuery");

	if (!PQsetSingleRowMode(ctx->db))
		die(NULL, "PQsetSingleRowMode");

	/* flush query */
	res = PQflush(db);
	if (res < 0)
		die(db, "flush 1");
	waitWrite = res > 0;

	/* read data */
	while (1) {
		/* sleep until event */
		db_wait(ctx->db, waitWrite);

		/* got event, process it */
		if (waitWrite) {
			/* still more to flush? */
			res = PQflush(db);
			if (res < 0)
				die(db, "flush 2");
			waitWrite = res > 0;
		} else {
			/* read result */
			res = socket_read_cb(ctx);
			if (res < 0)
				die(db, "socket_read_cb");
			if (res > 0)
				return;
			waitWrite = 0;
		}
	}
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

	/* set up socket */
	PQsetnonblocking(db, 1);

	ctx.db = db;
	exec_query(&ctx, q);

	PQfinish(db);

	return 0;
}

