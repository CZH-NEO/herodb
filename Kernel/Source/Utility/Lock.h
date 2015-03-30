/***********************************************************************
Vczh Library++ 3.0
Developer: Zihan Chen(vczh)
Database::Utility

***********************************************************************/

#ifndef VCZH_DATABASE_UTILITY_LOCK
#define VCZH_DATABASE_UTILITY_LOCK

#include "Buffer.h"

namespace vl
{
	namespace database
	{

/***********************************************************************
LockManager (Data Structure)
***********************************************************************/

		enum class LockTargetType
		{
			Table,
			Page,
			Row,
		};

		enum class LockTargetAccess
		{
			IntentShared			= 0, // parent of Shared
			Shared					= 1, // reading the object
			Update					= 2, // intent to update the object, can upgrade to Shared ot Exclusive
			IntentExclusive			= 3, // parent of Exclusive or Update
			SharedIntentExclusive	= 4, // enable a transaction to acquire Shared and IntentExclusive at the same time
			Exclusive				= 5, // writing the object
			NumbersOfLockTypes		= 6,
		};

#define LOCK_TYPES ((vint)LockTargetAccess::NumbersOfLockTypes)

		struct LockTarget
		{
			LockTargetType			type			= LockTargetType::Table;
			LockTargetAccess		access			= LockTargetAccess::Shared;
			BufferTable				table			= BufferTable::Invalid();
			union
			{
				BufferPage			page;
				BufferPointer		address;
			};

			LockTarget()
			{
			}
			
			LockTarget(LockTargetAccess _access, BufferTable _table)
				:type(LockTargetType::Table)
				,access(_access)
				,table(_table)
			{
			}
			
			LockTarget(LockTargetAccess _access, BufferTable _table, BufferPage _page)
				:type(LockTargetType::Page)
				,access(_access)
				,table(_table)
				,page(_page)
			{
			}
			
			LockTarget(LockTargetAccess _access, BufferTable _table, BufferPointer _address)
				:type(LockTargetType::Row)
				,access(_access)
				,table(_table)
				,address(_address)
			{
			}

			bool IsValid()
			{
				return table.IsValid();
			}

			static vint64_t Compare(const LockTarget& a, const LockTarget& b)
			{
				vuint64_t compare = 0;
				compare = (vint64_t)a.type - (vint64_t)b.type;
				if (compare != 0) return compare;
				compare = (vint64_t)a.access - (vint64_t)b.access;
				if (compare != 0) return compare;
				compare = (vint64_t)a.table.index - (vint64_t)b.table.index;
				if (compare != 0) return compare;
				switch (a.type)
				{
				case LockTargetType::Page:
					return (vint64_t)a.page.index - (vint64_t)b.page.index;
				case LockTargetType::Row:
					return (vint64_t)a.address.index - (vint64_t)b.address.index;
				default:
					return 0;
				}
			}

			bool operator==(const LockTarget& b)const { return Compare(*this, b) == 0; }
			bool operator!=(const LockTarget& b)const { return Compare(*this, b) != 0; }
			bool operator< (const LockTarget& b)const { return Compare(*this, b) <  0; }
			bool operator<=(const LockTarget& b)const { return Compare(*this, b) <= 0; }
			bool operator> (const LockTarget& b)const { return Compare(*this, b) >  0; }
			bool operator>=(const LockTarget& b)const { return Compare(*this, b) >= 0; }
		};

		struct LockResult
		{
			bool					blocked			= true;
			void*					lockedAddress	= nullptr;
		};

		struct DeadlockInfo
		{
			typedef collections::List<BufferTransaction>							TransactionList;
			typedef collections::Dictionary<BufferTransaction, LockTarget>			TransactionMap;
			typedef collections::Group<BufferTransaction, LockTarget>				TransactionGroup;

			TransactionGroup		acquired;
			TransactionMap			pending;
			TransactionList			rollbacks;
		};

/***********************************************************************
LockManager
***********************************************************************/

		class DeadlockDetection;

		class LockManager : public Object
		{
			friend class DeadlockDetection;
		protected:
			typedef collections::SortedList<LockTarget>								LockTargetList;

			struct TableInfo
			{
				BufferTable			table;
				BufferSource		source;
			};

			struct TransInfo
			{
				BufferTransaction	trans;
				vuint64_t			importance;
				LockTargetList		acquiredLocks;
				LockTarget			pendingLock;
			};

			typedef collections::Dictionary<BufferTable, Ptr<TableInfo>>			TableMap;
			typedef collections::Dictionary<BufferTransaction, Ptr<TransInfo>>		TransMap;

			BufferManager*			bm;
			SpinLock				lock;
			TableMap				tables;
			TransMap				transactions;

/***********************************************************************
LockManager (Lock Hierarchy)
***********************************************************************/

		protected:
			template<typename T>
			struct ObjectLockInfo
			{
				typedef T			ObjectType;

				SpinLock			lock;
				T					object;
				vint				acquiredLocks[LOCK_TYPES];

				ObjectLockInfo(const T& _object)
					:object(_object)
				{
					memset((void*)acquiredLocks, 0, sizeof(acquiredLocks));
				}

				virtual bool IsEmpty()
				{
					for (vint i = 0; i < LOCK_TYPES ; i++)
					{
						if (acquiredLocks[i])
						{
							return false;
						}
					}
					return true;
				}
			};

/***********************************************************************
LockManager (Lock Hierarchy -- Row)
***********************************************************************/
			
			struct RowLockInfo : ObjectLockInfo<vuint64_t>
			{
				RowLockInfo(vuint64_t offset)
					:ObjectLockInfo<vuint64_t>(offset)
				{
				}
			};

			typedef collections::Dictionary<vuint64_t, Ptr<RowLockInfo>>			RowLockMap;

/***********************************************************************
LockManager (Lock Hierarchy -- Page)
***********************************************************************/

			struct PageLockInfo : ObjectLockInfo<BufferPage>
			{
				RowLockMap			rowLocks;

				PageLockInfo(const BufferPage& page)
					:ObjectLockInfo<BufferPage>(page)
				{
				}

				bool IsEmpty()override
				{
					return rowLocks.Count() == 0 && ObjectLockInfo<BufferPage>::IsEmpty();
				}
			};

			typedef collections::Dictionary<BufferPage, Ptr<PageLockInfo>>			PageLockMap;

/***********************************************************************
LockManager (Lock Hierarchy -- Table)
***********************************************************************/

			struct TableLockInfo : ObjectLockInfo<BufferTable>
			{
				PageLockMap			pageLocks;

				TableLockInfo(const BufferTable& table)
					:ObjectLockInfo<BufferTable>(table)
				{
				}

				bool IsEmpty()override
				{
					return pageLocks.Count() == 0 && ObjectLockInfo<BufferTable>::IsEmpty();
				}
			};

			typedef collections::Array<Ptr<TableLockInfo>>							TableLockArray;

			TableLockArray			tableLocks;

/***********************************************************************
LockManager (Lock Hierarchy -- PendingLock)
***********************************************************************/

			typedef collections::SortedList<BufferTransaction>						PendingTransList;

			struct PendingInfo
			{
				PendingTransList	transactions;
				vint				lastTryIndex = -1;
			};

			typedef collections::Dictionary<vuint64_t, Ptr<PendingInfo>>			PendingMap;

			PendingMap				pendings;

/***********************************************************************
LockManager (ObjectLock)
***********************************************************************/

		protected:
			template<typename TInfo>
			bool					AcquireObjectLockUnsafe(Ptr<TInfo> lockInfo, Ptr<TransInfo> owner, const LockTarget& target);
			template<typename TInfo>
			bool					ReleaseObjectLockUnsafe(Ptr<TInfo> lockInfo, Ptr<TransInfo> owner, const LockTarget& target);
			Ptr<TransInfo>			CheckInputUnsafe(BufferTransaction owner, const LockTarget& target);
			bool					AddPendingLockUnsafe(Ptr<TransInfo> owner, const LockTarget& target);
			bool					RemovePendingLockUnsafe(Ptr<TransInfo> owner, const LockTarget& target);

/***********************************************************************
LockManager (Template)
***********************************************************************/

		protected:
			template<typename TArgs, typename... TLockInfos>
			using GenericLockHandler = bool(LockManager::*)(Ptr<TransInfo> owner, TArgs arguments, Ptr<TLockInfos>... lockInfo);
			template<typename TArgs>
			using PreLockHandler	= bool(LockManager::*)(Ptr<TransInfo> owner, TArgs arguments, bool& stopped);

			using AcquireLockArgs	= Tuple<const LockTarget&, LockResult&, bool>;
			using ReleaseLockArgs	= const LockTarget&;
			using UpgradeLockArgs	= Tuple<const LockTarget&, LockTargetAccess, LockResult&>;
			
			template<typename TArgs>
			using TableLockHandler	= GenericLockHandler<TArgs, TableLockInfo>;
			template<typename TArgs>
			using PageLockHandler	= GenericLockHandler<TArgs, TableLockInfo, PageLockInfo>;
			template<typename TArgs>
			using RowLockHandler	= GenericLockHandler<TArgs, TableLockInfo, PageLockInfo, RowLockInfo>;

			template<typename TArgs>
			bool					OperateObjectLock(BufferTransaction owner, TArgs arguments, PreLockHandler<TArgs> preLockHandler, TableLockHandler<TArgs> tableLockHandler, PageLockHandler<TArgs> pageLockHandler, RowLockHandler<TArgs> rowLockHandler, bool createLockInfo, bool checkPendingLock);

/***********************************************************************
LockManager (Acquire)
***********************************************************************/

		protected:
			template<typename TLockInfo>
			bool					AcquireGeneralLock(Ptr<TransInfo> owner, AcquireLockArgs arguments, Ptr<TLockInfo> lockInfo);
			bool					AcquireTableLock(Ptr<TransInfo> owner, AcquireLockArgs arguments, Ptr<TableLockInfo> tableLockInfo);
			bool					AcquirePageLock(Ptr<TransInfo> owner, AcquireLockArgs arguments, Ptr<TableLockInfo> tableLockInfo, Ptr<PageLockInfo> pageLockInfo);
			bool					AcquireRowLock(Ptr<TransInfo> owner, AcquireLockArgs arguments, Ptr<TableLockInfo> tableLockInfo, Ptr<PageLockInfo> pageLockInfo, Ptr<RowLockInfo> rowLockInfo);

/***********************************************************************
LockManager (Release)
***********************************************************************/

		protected:
			bool					ReleasePreLock(Ptr<TransInfo> owner, ReleaseLockArgs arguments, bool& stopped);
			bool					ReleaseTableLock(Ptr<TransInfo> owner, ReleaseLockArgs arguments, Ptr<TableLockInfo> tableLockInfo);
			bool					ReleasePageLock(Ptr<TransInfo> owner, ReleaseLockArgs arguments, Ptr<TableLockInfo> tableLockInfo, Ptr<PageLockInfo> pageLockInfo);
			bool					ReleaseRowLock(Ptr<TransInfo> owner, ReleaseLockArgs arguments, Ptr<TableLockInfo> tableLockInfo, Ptr<PageLockInfo> pageLockInfo, Ptr<RowLockInfo> rowLockInfo);

/***********************************************************************
LockManager (Upgrade)
***********************************************************************/

		protected:
			template<typename TLockInfo>
			bool					UpgradeGeneralLock(Ptr<TransInfo> owner, UpgradeLockArgs arguments, Ptr<TLockInfo> lockInfo);
			bool					UpgradeTableLock(Ptr<TransInfo> owner, UpgradeLockArgs arguments, Ptr<TableLockInfo> tableLockInfo);
			bool					UpgradePageLock(Ptr<TransInfo> owner, UpgradeLockArgs arguments, Ptr<TableLockInfo> tableLockInfo, Ptr<PageLockInfo> pageLockInfo);
			bool					UpgradeRowLock(Ptr<TransInfo> owner, UpgradeLockArgs arguments, Ptr<TableLockInfo> tableLockInfo, Ptr<PageLockInfo> pageLockInfo, Ptr<RowLockInfo> rowLockInfo);

/***********************************************************************
LockManager (UnsafeLockOperation)
***********************************************************************/

		protected:
			bool					AcquireLockUnsafe(BufferTransaction owner, const LockTarget& target, LockResult& result, bool processPendingLock);
			bool					ReleaseLockUnsafe(BufferTransaction owner, const LockTarget& target);
			bool					UpgradeLockUnsafe(BufferTransaction owner, const LockTarget& oldTarget, LockTargetAccess newAccess, LockResult& result);

/***********************************************************************
LockManager (Interface)
***********************************************************************/

		public:
			LockManager(BufferManager* _bm);
			~LockManager();

			bool					RegisterTable(BufferTable table, BufferSource source);
			bool					UnregisterTable(BufferTable table);
			bool					RegisterTransaction(BufferTransaction trans, vuint64_t importance);
			bool					UnregisterTransaction(BufferTransaction trans);

			bool					AcquireLock(BufferTransaction owner, const LockTarget& target, LockResult& result);
			bool					ReleaseLock(BufferTransaction owner, const LockTarget& target);
			bool					UpgradeLock(BufferTransaction owner, const LockTarget& oldTarget, LockTargetAccess newAccess, LockResult& result);
			bool					TableHasLocks(BufferTable table);

			BufferTransaction		PickTransaction(LockResult& result);
			void					DetectDeadlock(DeadlockInfo& info);
			bool					Rollback(BufferTransaction trans);
		};

#undef LOCK_TYPES

	}
}

#endif
