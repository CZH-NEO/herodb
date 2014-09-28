/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)
Database::Utility

***********************************************************************/

#ifndef VCZH_DATABASE_UTILITY_LOG
#define VCZH_DATABASE_UTILITY_LOG

#include "Buffer.h"

namespace vl
{
	namespace database
	{
		class LogTransDesc
		{
		public:
			BufferPointer				firstItem;
			BufferPointer				lastItem;
		};

		class ILogAccessor : public virtual Interface
		{
		public:
			virtual BufferTransaction	GetTransaction() = 0;
			virtual stream::IStream&	GetStream() = 0;
		};

		class ILogReader : public virtual ILogAccessor
		{
		public:
			virtual bool				NextItem() = 0;
		};

		class ILogWriter : public virtual ILogAccessor
		{
		public:
			virtual bool				IsOpening() = 0;
			virtual bool				Close() = 0;
		};

		class LogManager : public Object
		{
			typedef collections::Dictionary<BufferTransaction::IndexType, Ptr<LogTransDesc>>		TransMap;
			typedef collections::List<BufferPage>													PageList;

		private:
			BufferManager*				bm;
			BufferSource				source;
			bool						autoUnload;

			SpinLock					lock;
			vuint64_t					pageSize;
			vuint64_t					indexPageItemCount;
			volatile vuint64_t			usedTransactionCount;
			PageList					indexPages;
			TransMap					activeTransactions;
			BufferPointer				nextBlockAddress;

			bool						WriteAddressItem(BufferTransaction transaction, BufferPointer address);
			bool						AllocateBlock(vuint64_t minSize, vuint64_t& size, BufferPointer& address);
		public:
			LogManager(BufferManager* _bm, BufferSource _source, bool _createNew, bool _autoUnload = true);
			~LogManager();

			vuint64_t					GetUsedTransactionCount();
			BufferTransaction			GetTransaction(vuint64_t index);

			BufferTransaction			OpenTransaction();
			bool						CloseTransaction(BufferTransaction transaction);
			bool						IsActive(BufferTransaction transaction);

			Ptr<ILogWriter>				OpenLogItem(BufferTransaction transaction);
			Ptr<ILogReader>				EnumLogItem(BufferTransaction transaction);
			Ptr<ILogReader>				EnumInactiveLogItem(BufferTransaction transaction);
		};
	}
}

#endif
