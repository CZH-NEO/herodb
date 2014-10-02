#include "UnitTest.h"
#include "../Source/Utility/Log.h"

using namespace vl;
using namespace vl::database;
using namespace vl::collections;

extern WString GetTempFolder();
#define TEMP_DIR GetTempFolder()+
#define KB *1024
#define MB *1024*1024

TEST_CASE(Utility_Log_TransactionWithNoItem)
{
	BufferManager bm(4 KB, 16);
	auto source = bm.LoadFileSource(TEMP_DIR L"db.bin", true);
	LogManager log(&bm, source, true);

	auto trans = log.OpenTransaction();
	TEST_ASSERT(trans.IsValid());
	TEST_ASSERT(log.IsActive(trans) == true);

	auto reader = log.EnumLogItem(trans);
	TEST_ASSERT(reader);
	TEST_ASSERT(reader->GetTransaction().index == trans.index);
	TEST_ASSERT(!log.EnumInactiveLogItem(trans));
	TEST_ASSERT(reader->NextItem() == false);

	TEST_ASSERT(log.CloseTransaction(trans) == true);
	TEST_ASSERT(log.IsActive(trans) == false);

	reader = log.EnumInactiveLogItem(trans);
	TEST_ASSERT(reader);
	TEST_ASSERT(reader->GetTransaction().index == trans.index);
	TEST_ASSERT(!log.EnumLogItem(trans));
	TEST_ASSERT(reader->NextItem() == false);
}

TEST_CASE(Utility_Log_TransactionWithOneEmptyItem)
{
}

TEST_CASE(Utility_Log_TransactionWithOneNonEmptyItem)
{
}

TEST_CASE(Utility_Log_TransactionWithMultipleItems)
{
}

TEST_CASE(Utility_Log_OpenTransactionsSequencial)
{
}

TEST_CASE(Utility_Log_OpenTransactionsParallel)
{
}

TEST_CASE(Utility_Log_OpenInactiveTransaction)
{
}

TEST_CASE(Utility_Log_LoadExistingLog)
{
}
