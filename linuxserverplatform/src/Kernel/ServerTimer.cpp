#include "CommonHead.h"
#include "DataLine.h"
#include "log.h"
#include "Function.h"
#include "ServerTimer.h"


// 定时器精度，单位毫秒。
#define SERVER_TIME_ONCE	100

// 定时器测试代码宏
//#define TEST_TIMER_CODE

#ifdef TEST_TIMER_CODE
struct timeval g_lasttime;
#endif

struct TimerParam
{
	CServerTimer* pCServerTimer;
	struct event_base* base;
};

CServerTimer::CServerTimer()
{
	m_bRun = false;
	m_pDataLine = NULL;
	m_pLock = new CSignedLock;
}

CServerTimer::~CServerTimer()
{
	m_timerMap.clear();
	delete m_pLock;
	m_pLock = NULL;
}

bool CServerTimer::Start(CDataLine* pDataLine)
{
	INFO_LOG("CServerTimer thread begin...");

	if (!pDataLine)
	{
		CON_ERROR_LOG("pDataLine == NULL");
		return false;
	}

	m_pDataLine = pDataLine;
	m_bRun = true;

	// 开辟线程
	pthread_t threadID = 0;
	int err = pthread_create(&threadID, NULL, ThreadCheckTimer, (void*)this);
	if (err != 0)
	{
		SYS_ERROR_LOG("ThreadCheckTimer failed");
		return false;
	}

	return true;
}

bool CServerTimer::Stop()
{
	INFO_LOG("CServerTimer thread exit.");

	m_bRun = false;
	m_timerMap.clear();

	return true;
}

bool CServerTimer::SetTimer(unsigned int uTimerID, unsigned int uElapse, BYTE timerType /*= SERVERTIMER_TYPE_PERISIST*/)
{
	if (uElapse < SERVER_TIME_ONCE)
	{
		uElapse = SERVER_TIME_ONCE;
	}

	ServerTimerInfo info;

	info.elapse = uElapse / SERVER_TIME_ONCE * SERVER_TIME_ONCE;
	info.starttime = GetSysMilliseconds() / SERVER_TIME_ONCE * SERVER_TIME_ONCE + uElapse;
	info.timertype = timerType;

	CSignedLockObject lock(m_pLock, true);

	m_timerMap[uTimerID] = info;

	return true;
}

bool CServerTimer::KillTimer(unsigned int uTimerID)
{
	if (!ExistsTimer(uTimerID))
	{
		return false;
	}

	CSignedLockObject lock(m_pLock, true);

	auto iter = m_timerMap.find(uTimerID);
	if (iter == m_timerMap.end())
	{
		return false;
	}

	m_timerMap.erase(iter);

	return true;
}

bool CServerTimer::ExistsTimer(unsigned int uTimerID)
{
	auto iter = m_timerMap.find(uTimerID);
	if (iter == m_timerMap.end())
	{
		return false;
	}

	return true;
}

void CServerTimer::TimeoutCB(evutil_socket_t fd, short event, void* arg)
{
	struct TimerParam* param = (struct TimerParam*)arg;

	if (param == NULL)
	{
		CON_ERROR_LOG("TimeoutCB 参数为空");
		return;
	}

	// 线程终止运行
	if (!param->pCServerTimer->m_bRun)
	{
		event_base_loopbreak(param->base);
	}

#ifdef TEST_TIMER_CODE
	struct timeval newtime, difference;
	double elapsed;
	evutil_gettimeofday(&newtime, NULL);
	evutil_timersub(&newtime, &g_lasttime, &difference);
	elapsed = difference.tv_sec +
		(difference.tv_usec / 1.0e6);

	printf("timeout_cb called at %ld: %.3f seconds elapsed.\n",
		newtime.tv_sec, elapsed);
	g_lasttime = newtime;
#endif

	long long currTime = GetSysMilliseconds() / SERVER_TIME_ONCE * SERVER_TIME_ONCE;

	// lock
	CSignedLockObject lock(param->pCServerTimer->m_pLock, false);
	lock.Lock();

	for (auto iter = param->pCServerTimer->m_timerMap.begin(); iter != param->pCServerTimer->m_timerMap.end();)
	{
		if ((currTime >= iter->second.starttime) && (currTime - iter->second.starttime) % iter->second.elapse == 0)
		{
			ServerTimerLine WindowTimer;
			WindowTimer.uTimerID = iter->first;
			param->pCServerTimer->m_pDataLine->AddData(&WindowTimer.LineHead, sizeof(WindowTimer), HD_TIMER_MESSAGE);

			if (iter->second.timertype == SERVERTIMER_TYPE_SINGLE)
			{
				param->pCServerTimer->m_timerMap.erase(iter++);
				continue;
			}
		}

		iter++;
	}

	lock.UnLock();
}

void* CServerTimer::ThreadCheckTimer(void* pThreadData)
{
	CServerTimer* pThis = (CServerTimer*)pThreadData;
	if (!pThis)
	{
		pthread_exit(NULL);
	}

	struct event timeout;
	struct event_base* base;

	/* Initialize the event library */
	base = event_base_new();

	TimerParam param;
	param.base = base;
	param.pCServerTimer = pThis;

	/* Initialize one event */
	event_assign(&timeout, base, -1, EV_PERSIST, CServerTimer::TimeoutCB, (void*)& param);

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = SERVER_TIME_ONCE * 1000;
	event_add(&timeout, &tv);

#ifdef TEST_TIMER_CODE
	evutil_gettimeofday(&g_lasttime, NULL);
#endif

	event_base_dispatch(base);

	pthread_exit(NULL);
}