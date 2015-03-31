#include "InMemoryBuffer.h"
#include <stdlib.h>
#include <time.h>

namespace vl
{
	namespace database
	{
		using namespace collections;

/***********************************************************************
InMemoryBufferSource
***********************************************************************/

		Ptr<BufferPageDesc> InMemoryBufferSource::MapPage(BufferPage page)
		{
			CHECK_ERROR(page.index <= pages.Count(), L"vl::database::InMemoryBufferSource::MapPage(BufferPage)#Internal error: Index of page to map is out of range.");
			if (page.index < pages.Count() && pages[page.index])
			{
				auto pageDesc = pages[page.index];
				pageDesc->lastAccessTime = (vuint64_t)time(nullptr);
				return pageDesc;
			}
			else
			{
				auto address = malloc(pageSize);
				if (!address) return nullptr;

				auto pageDesc = MakePtr<BufferPageDesc>();
				pageDesc->address = address;
				pageDesc->offset = page.index * pageSize;
				pageDesc->lastAccessTime = (vuint64_t)time(nullptr);

				if (page.index == pages.Count())
				{
					pages.Add(pageDesc);
				}
				else
				{
					pages[page.index] = pageDesc;
				}
				INCRC(totalUsedPages);
				return pageDesc;
			}
		}

		InMemoryBufferSource::InMemoryBufferSource(BufferSource _source, volatile vuint64_t* _totalUsedPages, vuint64_t _pageSize)
			:source(_source)
			,totalUsedPages(_totalUsedPages)
			,pageSize(_pageSize)
		{
			indexPage = AllocatePage();
		}

		void InMemoryBufferSource::Unload()
		{
			FOREACH(Ptr<BufferPageDesc>, pageDesc, pages)
			{
				if (pageDesc)
				{
					DECRC(totalUsedPages);
					free(pageDesc->address);
				}
			}
		}

		BufferSource InMemoryBufferSource::GetBufferSource()
		{
			return source;
		}

		SpinLock& InMemoryBufferSource::GetLock()
		{
			return lock;
		}

		WString InMemoryBufferSource::GetFileName()
		{
			return L"";
		}

		bool InMemoryBufferSource::InMemoryBufferSource::UnmapPage(BufferPage page)
		{
			if (page.index >= pages.Count())
			{
				return false;
			}

			auto pageDesc = pages[page.index];
			if (!pageDesc || pageDesc->locked)
			{
				return false;
			}

			free(pageDesc->address);
			pages[page.index] = nullptr;
			freePages.Add(page.index);
			DECRC(totalUsedPages);
			return true;
		}

		BufferPage InMemoryBufferSource::GetIndexPage()
		{
			return indexPage;
		}

		BufferPage InMemoryBufferSource::AllocatePage()
		{
			BufferPage page = BufferPage::Invalid();
			if (freePages.Count() > 0)
			{
				page.index = freePages[freePages.Count() - 1];
				freePages.RemoveAt(freePages.Count() - 1);
			}
			else
			{
				page.index = pages.Count();
			}

			if (MapPage(page))
			{
				return page;
			}
			else
			{
				return BufferPage::Invalid();
			}
		}

		bool InMemoryBufferSource::FreePage(BufferPage page)
		{
			if (page.index == indexPage.index)
			{
				return false;
			}
			return UnmapPage(page);
		}

		void* InMemoryBufferSource::LockPage(BufferPage page)
		{
			if (page.index >= pages.Count())
			{
				return nullptr;
			}

			auto pageDesc = pages[page.index];
			if (!pageDesc || pageDesc->locked)
			{
				return nullptr;
			}

			pageDesc->locked = true;
			return pageDesc->address;
		}

		bool InMemoryBufferSource::UnlockPage(BufferPage page, void* address, PersistanceType persistanceType)
		{
			if (page.index >= pages.Count())
			{
				return false;
			}

			auto pageDesc = pages[page.index];
			if (!pageDesc || !pageDesc->locked || address != pageDesc->address)
			{
				return false;
			}

			pageDesc->locked = false;
			return true;
		}

		void InMemoryBufferSource::FillUnmapPageCandidates(collections::List<BufferPageTimeTuple>& pages, vint expectCount)
		{
		}

		IBufferSource* CreateMemorySource(BufferSource source, volatile vuint64_t* totalUsedPages, vuint64_t pageSize)
		{
			return new InMemoryBufferSource(source, totalUsedPages, pageSize);
		}
	}
}
