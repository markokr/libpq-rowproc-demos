
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>
#include <errno.h>
#include <poll.h>

#include <libpq-fe.h>

struct Context {
	PGconn *db;
	PGresult *temp_res;
	PGrowValue *temp_columns;

	jmp_buf exc;
};

static int scenario;

static void die(const char *msg, ...)
{
	va_list ap;
	char buf[512];

	va_start(ap, msg);
	vsnprintf(buf, sizeof(buf), msg, ap);
	va_end(ap);

	fprintf(stderr, "fatal: %s\n", buf);
	exit(1);
}

static void dbdie(PGconn *db, const char *msg)
{
	die("%s: %s", PQerrorMessage(db));
}

static void proc_row(struct Context *ctx, PGresult *res, PGrowValue *columns)
{
	if (0)
	printf("column: %.*s\n",
		   columns[0].len,
		   columns[0].value);
}

static void proc_result(struct Context *ctx, PGresult *r)
{
	ExecStatusType s;

	s = PQresultStatus(r);
	if (s == PGRES_TUPLES_OK)
		printf("%s\n", PQresStatus(s));
	else
		printf("%s: %s\n", PQresStatus(s), PQerrorMessage(ctx->db));
	PQclear(r);
}

static int my_handler(PGresult *res, PGrowValue *columns, void *arg)
{
	struct Context *ctx = arg;
	switch (scenario) {
	case 1:
		proc_row(ctx, res, columns);
		return 1;
	case 0:
		PQsetRowProcessorErrMsg(res, "std rowproc error");
		return 0;
	case 2:
		ctx->temp_res = res;
		ctx->temp_columns = columns;
		return 2;
	case 3:
	case 4:
		longjmp(ctx->exc, 1);
	default:
		return scenario;
	}
}

/* this handles socket read event */
static int socket_read_cb(struct Context *ctx, PGconn *db)
{
	PGresult *r;

	/* read incoming data */
	if (!PQconsumeInput(db))
		return -1;

	/* be ready to handle row data */
	ctx->temp_columns = NULL;
	ctx->temp_res = NULL;

	/*
	 * one query may result in several PGresult's,
	 * wrap everything in one big loop.
	 */
	while (1) {

		if (PQisBusy(db)) {
			if (ctx->temp_columns) {
				proc_row(ctx, ctx->temp_res, ctx->temp_columns);

				ctx->temp_columns = NULL;
				ctx->temp_res = NULL;

				continue;
			}

			/* need to wait for more data from network */
			return 0;
		}

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
static void proc_async(struct Context *ctx, PGconn *db, const char *q)
{
	int res;
	int wait;
	struct pollfd pfd;

	/* test async draining */
	if (setjmp(ctx->exc))
		goto exception;

	/* set up socket */
	PQsetnonblocking(db, 1);
	pfd.fd = PQsocket(db);

	/* launch query */
	if (!PQsendQuery(db, q))
		dbdie(db, "PQsendQuery");

	res = PQflush(db); // -1:err, 0:ok, 1:more
	if (res < 0)
		dbdie(db, "flush 1");
	wait = (res > 0) ? POLLOUT : POLLIN;

	/* read data */
	while (1) {
		pfd.events = wait;
		pfd.revents = 0;
		res = poll(&pfd, 1, 1000);
		if (res < 0 && errno == EINTR) {
			continue;
		} else if (res < 0) {
			die("poll: %s", strerror(errno));
		} else if (res == 0) {
			continue;
		}

		/* got event, process it */
		if (wait == POLLOUT) {
			res = PQflush(db); // -1:err, 0:ok, 1:more
			if (res < 0)
				dbdie(db, "flush 2");
			wait = (res > 0) ? POLLOUT : POLLIN;
		} else {
			res = socket_read_cb(ctx, db);
			if (res < 0)
				dbdie(db, "PQconsumeInput");
			if (res > 0)
				return;
			wait = POLLIN;
		}
	}
	return;
exception:
	/* drain rows */
	scenario = 1;
	printf("got exception, draining rows\n");

	{
		PGresult *r;
		r = PQgetResult(db);
		printf("final status: %s\n", PQresStatus(PQresultStatus(r)));
		PQclear(r);
	}
}

int main(int argc, char *argv[])
{
	const char *connstr = CONNSTR;
	//const char *q = "select lpad('1', 60, '0') from generate_series(1,1000000);";
	const char *q = "show all;";
	PGconn *db;
	struct Context main_ctx;

	if (argc != 2) {
		printf("usage: %s [0|1|2|3]\n", argv[0]);
		return 1;
	}
	scenario = atoi(argv[1]);

	db = PQconnectdb(connstr);
	if (!db || PQstatus(db) == CONNECTION_BAD)
		dbdie(db, "connect");
	main_ctx.db = db;

	PQsetRowProcessor(db, my_handler, &main_ctx);

	proc_async(&main_ctx, db, q);
	PQfinish(db);

	return 0;
}

