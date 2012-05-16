/*
 * Row processor sync demo.
 *
 * usage: rowproc-sync [connstr [query]]
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>

#include <libpq-fe.h>

struct Context {
	PGconn *db;
	int count;
};

static void die(PGconn *db, const char *msg)
{
	fprintf(stderr, "%s: %s", msg, PQerrorMessage(db));
	exit(1);
}

static int my_handler(PGresult *res, const PGdataValue *columns, const char **errmsgp, void *arg)
{
	struct Context *ctx = arg;

	if (!columns)
		return 1;

	ctx->count++;

	return 1;
}

static void exec_query(struct Context *ctx, const char *q)
{
	PGresult *r;

	ctx->count = 0;
	PQsetRowProcessor(ctx->db, my_handler, ctx);

	r = PQexec(ctx->db, q);

	/* check final result */
	if (!r || PQresultStatus(r) != PGRES_TUPLES_OK)
		die(ctx->db, "select");
	else
		printf("query successful, got %d rows\n", ctx->count);
	PQclear(r);

	PQsetRowProcessor(ctx->db, NULL, NULL);
}


int main(int argc, char *argv[])
{
	const char *connstr;
	const char *q;
	struct Context ctx;

	connstr = "dbname=postgres";
	if (argc > 1)
		connstr = argv[1];

	q = "show all";
	if (argc > 2)
		q = argv[2];

	ctx.db = PQconnectdb(connstr);
	if (!ctx.db || PQstatus(ctx.db) == CONNECTION_BAD)
		die(ctx.db, "connect");

	exec_query(&ctx, q);

	PQfinish(ctx.db);

	return 0;
}

