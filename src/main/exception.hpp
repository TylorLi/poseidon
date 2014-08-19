#ifndef POSEIDON_EXCEPTION_HPP_
#define POSEIDON_EXCEPTION_HPP_

#include <string>
#include <exception>
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <boost/cstdint.hpp>
#include <string.h>
#include <errno.h>

namespace Poseidon {

class Exception : public std::exception {
protected:
	const char *const m_file;
	const std::size_t m_line;
	const std::string m_what;

public:
	Exception(const char *file, std::size_t line, const std::string &what)
		: m_file(file), m_line(line), m_what(what)
	{
	}
	~Exception() throw() {
	}

public:
	const char *file() const throw() {
		return m_file;
	}
	std::size_t line() const throw() {
		return m_line;
	}
	const char *what() const throw() {
		return m_what.c_str();
	}
};

class SystemError : public Exception {
private:
	static std::string stringFromErrno(int code){
		std::string ret;
		ret.resize(127);
		int count;
#ifdef __GLIBC__
		const char *const result = ::strerror_r(code, &ret[0], ret.size());
		if(result != NULL){
			ret.assign(result);
			count = ret.size();
#else
		int result = ::strerror_r(code, &ret[0], ret.size());
		if(result == 0){
			count = std::strlen(ret.c_str());
#endif
		} else {
			count = ::snprintf(&ret[0], ret.size(), "Unknown errno %d", code);
			if(count < 0){
				count = 0;
			}
		}
		ret.resize(count);
		return ret;
	}

private:
	const int m_code;

public:
	SystemError(const char *file, std::size_t line, int code = errno)
		: Exception(file, line, stringFromErrno(code)), m_code(code)
	{
	}

public:
	int code() const throw() {
		return m_code;
	}
};

class ProtocolException : public Exception {
public:
	enum {
		ERR_BROKEN_HEADER	= -1,
		ERR_TRUNCATED_BODY	= -2,
		ERR_END_OF_STREAM	= -3,
		ERR_BAD_REQUEST		= -4,
		ERR_INTERNAL_ERROR	= -5,
	};

private:
	const int m_code;

public:
	ProtocolException(const char *file, std::size_t line, const std::string &what, int code)
		: Exception(file, line, what), m_code(code)
	{
	}

public:
	int code() const throw() {
		return m_code;
	}
};

}

#define DEBUG_THROW(etype, ...)	\
	throw etype(__FILE__, __LINE__, __VA_ARGS__)

#endif