
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>

#include <libpq-fe.h>

struct Context {
	PGconn *db;
	int scenario;
	jmp_buf exc;
};


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
	die("%s: %s", msg, PQerrorMessage(db));
}

static void proc_sync(struct Context *ctx, const char *q)
{
	PGresult *r;

	if (setjmp(ctx->exc))
		goto exception;

	r = PQexec(ctx->db, q);
	if (!r || PQresultStatus(r) != PGRES_TUPLES_OK)
		dbdie(ctx->db, "select");
	else
		printf("query ok\n");
	PQclear(r);
	return;

exception:
	printf("exception, skipping rows\n");
	ctx->scenario = 1;
	while (1) {
		r = PQgetResult(ctx->db);
		if (!r)
			break;
		printf("final status: %s\n", PQresStatus(PQresultStatus(r)));
		PQclear(r);
	}
	printf("got NULL result\n");
}


static int my_handler(PGresult *res, PGrowValue *columns, void *arg)
{
	struct Context *ctx = arg;

	switch (ctx->scenario) {
	case 1:
		return 1;
	case -1:
		return -1;
	case 3:
		longjmp(ctx->exc, 1);
	default:
		/* should cause libpq error */
		return ctx->scenario;
	}
}

int main(int argc, char *argv[])
{
	const char *connstr = CONNSTR;
	const char *q = "show all";
	struct Context main_ctx;

	if (argc != 2) {
		printf("usage: %s [-1|0|1|3]\n", argv[0]);
		return 1;
	}

	main_ctx.scenario = atoi(argv[1]);

	main_ctx.db = PQconnectdb(connstr);
	if (!main_ctx.db || PQstatus(main_ctx.db) == CONNECTION_BAD)
		dbdie(main_ctx.db, "connect");

	PQsetRowProcessor(main_ctx.db, my_handler, &main_ctx);

	proc_sync(&main_ctx, q);
	PQfinish(main_ctx.db);

	return 0;
}

