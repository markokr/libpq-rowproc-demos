
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>

#include <libpq-fe.h>


static void die(PGconn *db, const char *msg)
{
	printf("%s: %s\n", msg, PQerrorMessage(db));
	exit(1);
}

static void proc_sync(PGconn *db, const char *q)
{
	PGresult *r;
	ExecStatusType s;

	if (!PQsendQuery(db, q))
		die(db, "PQsendQuery");
	while (1) {
		r = PQgetRow(db);
		if (!r)
			break;
		PQclear(r);
	}
	r = PQgetResult(db);
	s = PQresultStatus(r);
	if (s == PGRES_TUPLES_OK)
		printf("%s\n", PQresStatus(s));
	else
		printf("%s: %s\n", PQresStatus(s), PQerrorMessage(db));
	PQclear(r);
}


int main(int argc, char *argv[])
{
	const char *connstr = CONNSTR;
	const char *q = "show all";
	PGconn *db;

	db = PQconnectdb(connstr);
	if (!db || PQstatus(db) == CONNECTION_BAD)
		die(db, "connect");

	proc_sync(db, q);
	PQfinish(db);

	return 0;
}

