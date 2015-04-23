// 这个文件是 Poseidon 服务器应用程序框架的一部分。
// Copyleft 2014 - 2015, LH_Mouse. All wrongs reserved.

#include "../precompiled.hpp"
#include "thread_context.hpp"
#include "connection.hpp"
#include <boost/thread/once.hpp>
#include <mysql/mysql.h>
#include "../exception.hpp"
#include "../log.hpp"

namespace Poseidon {

namespace MySql {
	namespace {
		boost::once_flag g_mysqlInitFlag;

		__thread std::size_t t_initCount = 0;

		void initMySql(){
			LOG_POSEIDON_INFO("Initializing MySQL library...");

			if(::mysql_library_init(0, NULLPTR, NULLPTR) != 0){
				LOG_POSEIDON_FATAL("Could not initialize MySQL library.");
				std::abort();
			}

			std::atexit(&::mysql_library_end);
		}
	}

	ThreadContext::ThreadContext(){
		if(++t_initCount == 1){
			boost::call_once(&initMySql, g_mysqlInitFlag);

			LOG_POSEIDON_INFO("Initializing MySQL thread...");

			if(::mysql_thread_init() != 0){
				LOG_POSEIDON_FATAL("Could not initialize MySQL thread.");
				DEBUG_THROW(Exception, SSLIT("::mysql_thread_init() failed"));
			}
		}
	}
	ThreadContext::~ThreadContext(){
		if(--t_initCount == 0){
			::mysql_thread_end();
		}
	}
}

}
