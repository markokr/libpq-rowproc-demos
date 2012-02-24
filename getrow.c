
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
	return 2;
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
PQgetRow(PGconn *db, PGresult **hdr_p, PGrowValue **row_p)
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
 * Test code follows
 */

static void die(PGconn *db, const char *msg)
{
	printf("%s: %s\n", msg, PQerrorMessage(db));
	exit(1);
}

static void proc_row(PGresult *res, PGrowValue *columns)
{
	printf("column: %.*s\n",
		   columns[0].len,
		   columns[0].value);
}

static void proc_stream(PGconn *db, const char *q)
{
	int rc;
	PGresult *hdr;
	PGresult *final;
	PGrowValue *row;
	ExecStatusType s;

	/* send query */
	if (!PQsendQuery(db, q))
		die(db, "PQsendQuery");

	/* fetch rows one-by-one */
	while (1) {
		rc = PQgetRow(db, &hdr, &row);
		if (rc > 0)
			proc_row(hdr, row);
		else if (rc == 0)
			break;
		else
			die(db, "streamResult");
	}
	/* final PGresult, either PGRES_TUPLES_OK or error */
	final = PQgetResult(db);

	s = PQresultStatus(final);
	if (s == PGRES_TUPLES_OK)
		printf("%s\n", PQresStatus(s));
	else
		printf("%s: %s\n", PQresStatus(s), PQerrorMessage(db));
	PQclear(final);
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

