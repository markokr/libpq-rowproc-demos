/*
 * PQsetSingleRowMode sync demo.
 *
 * usage: onerow-sync [connstr [query]]
 */


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
		fprintf(stderr, "%s: %s\n", msg, PQerrorMessage(db));
	else
		fprintf(stderr, "%s\n", msg);
	exit(1);
}

static void exec_query(struct Context *ctx, const char *q)
{
	PGconn *db = ctx->db;
	PGresult *r;
	ExecStatusType s;

	ctx->count = 0;

	if (!PQsendQuery(db, q))
		die(db, "PQsendQuery");

	if (!PQsetSingleRowMode(db))
		die(NULL, "PQsetSingleRowMode");

	/* loop until all resultset is done */
	while (1) {

		/* get next result */
		r = PQgetResult(db);
		if (!r)
			break;
		s = PQresultStatus(r);
		switch (s) {
			case PGRES_TUPLES_OK:
				printf("query successful, got %d rows\n", ctx->count);
				ctx->count = 0;
				break;
			case PGRES_SINGLE_TUPLE:
				ctx->count++;
				break;
			default:
				printf("result: %s\n",PQresStatus(s));
				break;
		}

		PQclear(r);
	}
}


int main(int argc, char *argv[])
{
	const char *connstr;
	const char *q = "show all";
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
	exec_query(&ctx, q);

	PQfinish(db);

	return 0;
}

