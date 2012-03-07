
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <poll.h>

#include <libpq-fe.h>

/* temp buffer to pass pointers */
struct GetRowBuf {
	PGresult *temp_hdr;
	PGrowValue *temp_row;
};

/* set pointers, do early exit from PQisBusy() */
static int _gr_rowproc(PGresult *hdr, PGrowValue *row, void *arg)
{
	struct GetRowBuf *buf = arg;
	buf->temp_hdr = hdr;
	buf->temp_row = row;
	return 0;
}

/* wait for event on connection */
static void _gr_poll(PGconn *db, int pollev)
{
	struct pollfd pfd;
	int rc;

	pfd.fd = PQsocket(db);
	pfd.events = pollev;
	pfd.revents = 0;
loop:
	rc = poll(&pfd, 1, -1);
	if (rc < 0 && errno == EINTR)
		goto loop;
}

/*
 * Wait and return next row in resultset.
 *
 * returns:
 *   1 - got row data, the pointers are owned by PGconn
 *   0 - result done, use PQgetResult() to get final result
 *  -1 - some problem, check connection error
 */
static int
QPrecvRow(PGconn *db, PGresult **hdr_p, PGrowValue **row_p)
{
	struct GetRowBuf buf;
	int rc;
	int ret = -1;
	PQrowProcessor oldproc;
	void *oldarg;

	*hdr_p = NULL;
	*row_p = NULL;

	/* the query may be still pending, send it */
	while (1) {
		rc = PQflush(db);
		if (rc < 0)
			return -1;
		if (rc == 0)
			break;
		_gr_poll(db, POLLOUT);
	}

	/* replace row processor */
	oldproc = PQgetRowProcessor(db, &oldarg);
	PQsetRowProcessor(db, _gr_rowproc, &buf);

	/* read data */
	while (1) {
		buf.temp_hdr = NULL;
		buf.temp_row = NULL;

		/* done with resultset? */
		if (!PQisBusy(db))
			break;

		/* new row available? */
		if (buf.temp_row) {
			*hdr_p = buf.temp_hdr;
			*row_p = buf.temp_row;
			ret = 1;
			goto done;
		}

		/* more data needed */
		_gr_poll(db, POLLIN);
		if (!PQconsumeInput(db))
			goto done;
	}

	/* resultset is done and final PGresult is available */
	ret = 0;
done:
	/* restore old row processor */
	PQsetRowProcessor(db, oldproc, oldarg);
	return ret;
}

/*
 * Returns:
 *   PGresult - new 1-line result row, with status PG_COPYRES_ATTRS
 *   NULL - end of rows or error
 */
PGresult *QPgetRow(PGconn *db)
{
	int rc;
	int i;
	PGresult *hdr, *res;
	PGrowValue *row;

	rc = QPrecvRow(db, &hdr, &row);
	if (rc != 1)
		return NULL;

	res = PQcopyResult(hdr, PG_COPYRES_ATTRS);
	if (!res)
		goto nomem;
	for (i = 0; i < PQnfields(res); i++)
	{
		if (!PQsetvalue(res, 0, i, row[i].value, row[i].len))
			goto nomem;
	}
	return res;
nomem:
	PQclear(res);
	return NULL;
}

/*
 * Test code follows
 */

static void die(PGconn *db, const char *msg)
{
	printf("%s: %s\n", msg, PQerrorMessage(db));
	exit(1);
}

static void proc_row(PGresult *res)
{
	printf("column: %s\n", PQgetvalue(res, 0, 0));
}

static void proc_stream(PGconn *db, const char *q)
{
	PGresult *res;
	ExecStatusType s;

	/* send query */
	if (!PQsendQuery(db, q))
		die(db, "PQsendQuery");

	/* fetch rows one-by-one */
	while (1) {
		res = QPgetRow(db);
		if (!res)
			break;
		proc_row(res);
		PQclear(res);
	}
	/* final PGresult, either PGRES_TUPLES_OK or error */
	res = PQgetResult(db);

	s = PQresultStatus(res);
	if (s == PGRES_TUPLES_OK)
		printf("%s\n", PQresStatus(s));
	else
		printf("%s: %s\n", PQresStatus(s), PQerrorMessage(db));
	PQclear(res);
}

int main(int argc, char *argv[])
{
	PGconn *db;
	const char *connstr = CONNSTR;
	const char *q = "select lpad('1', 60, '0') || '  ' || x::text"
			" from generate_series(1,1000) x;";

	db = PQconnectdb(connstr);
	if (!db || PQstatus(db) == CONNECTION_BAD)
		die(db, "connect");

	PQsetnonblocking(db, 1);

	proc_stream(db, q);

	PQfinish(db);

	return 0;
}

