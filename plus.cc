
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <libpq-fe.h>

static int scenario;

// C callback that wraps class method
extern "C" int myconnection_rowproc_helper(PGresult *res, void *arg, PGrowValue *columns);

// connection class
class MyConnection {
	PGconn *db;
  protected:
	int process_row(PGresult *res, PGrowValue *columns);
	friend int myconnection_rowproc_helper(PGresult *res, void *arg, PGrowValue *columns);
  public:
	MyConnection() { db = NULL; }
	~MyConnection() { disconnect(); }
	void connect(const char *connstr);
	void disconnect(void);
	void exec(const char *q);
	void drain(void);
};

// custom exception class
class MyException : public std::runtime_error {
  public:
	MyException(const char *msg) : std::runtime_error(msg) { }
};

// 
class RowProcException : public MyException {
  public:
	RowProcException(const char *msg) : MyException(msg) { }
};

// call actual class method
int myconnection_rowproc_helper(PGresult *res, void *arg, PGrowValue *columns)
{
	MyConnection *c = (MyConnection *)arg;
	return c->process_row(res, columns);
}

// actual row processor
int MyConnection::process_row(PGresult *res, PGrowValue *columns)
{
	const char *msg = "process_row.set error";
	switch (scenario) {
	case 1:
		return 1;
	case 0:
		PQsetRowProcessorErrMsg(res, (char *)msg);
		return 0;
	case 3:
		// exception will be hidden before next query
		throw RowProcException("RowProcException");
	default:
		// exception cancels processing
		throw MyException("process_row exception");
	}
}

// drop remaining rows
void MyConnection::drain(void)
{
	PGresult *r;
	scenario = 1;
	r = PQgetResult(db);
	PQclear(r);
}

// connect to db
void MyConnection::connect(const char *connstr)
{
	db = PQconnectdb(connstr);
	if (!db)
		throw MyException("PQconnectdb OOM");
	if (PQstatus(db) == CONNECTION_BAD)
		throw MyException(PQerrorMessage(db));
	PQsetRowProcessor(db, myconnection_rowproc_helper, this);
}

// disconnect
void MyConnection::disconnect(void)
{
	if (db) {
		PQfinish(db);
		db = NULL;
	}
}

// run query
void MyConnection::exec(const char *q)
{
	PGresult *r;
	ExecStatusType s;

	r = PQexec(db, q);
	s = PQresultStatus(r);
	PQclear(r);
	if (s != PGRES_TUPLES_OK)
		throw MyException(PQerrorMessage(db));
}

int main(int argc, char *argv[])
{
	const char *connstr = CONNSTR;
	const char *q = "show all";

	//MyConnection *c = new MyConnection();
	std::auto_ptr<MyConnection> c (new MyConnection());

	if (argc != 2) {
		std::cout << "usage: ./plus [0|1|2|3]\n";
		return 1;
	}
	scenario = std::atoi(argv[1]);

	try {
		c->connect(connstr);
		c->exec(q);
		std::cout << "query ok\n";
	} catch (RowProcException &e) {
		std::cout << "got RowProcException, dropping remaining rows\n";
		c->drain();

		std::cout << "running query again\n";
		scenario = 1;
		c->exec(q);
		std::cout << "ok\n";
	} catch (std::exception &e) {
		std::cout << "Caught Error: " << e.what() << '\n';
	}

	return 0;
}

