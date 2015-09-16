// 这个文件是 Poseidon 服务器应用程序框架的一部分。
// Copyleft 2014 - 2015, LH_Mouse. All wrongs reserved.

#include "../precompiled.hpp"
#include "session.hpp"
#include "exception.hpp"
#include "control_message.hpp"
#include "../singletons/main_config.hpp"
#include "../log.hpp"
#include "../profiler.hpp"
#include "../job_base.hpp"
#include "../time.hpp"

namespace Poseidon {

namespace Cbpp {
	class Session::SyncJobBase : public JobBase {
	private:
		const boost::weak_ptr<Session> m_session;

	protected:
		explicit SyncJobBase(const boost::shared_ptr<Session> &session)
			: m_session(session)
		{
		}

	private:
		boost::weak_ptr<const void> getCategory() const FINAL {
			return m_session;
		}
		void perform() const FINAL {
			PROFILE_ME;

			const AUTO(session, m_session.lock());
			if(!session){
				return;
			}

			try {
				perform(session);
			} catch(Exception &e){
				LOG_POSEIDON(Logger::SP_MAJOR | Logger::LV_INFO,
					"Cbpp::Exception thrown: statusCode = ", e.statusCode(), ", what = ", e.what());
				try {
					session->sendError(ControlMessage::ID, e.statusCode(), e.what());
					session->shutdownRead();
					session->shutdownWrite();
				} catch(...){
					session->forceShutdown();
				}
				throw;
			} catch(std::exception &e){
				LOG_POSEIDON(Logger::SP_MAJOR | Logger::LV_INFO, "std::exception thrown: what = ", e.what());
				session->forceShutdown();
				throw;
			} catch(...){
				LOG_POSEIDON(Logger::SP_MAJOR | Logger::LV_INFO, "Unknown exception thrown.");
				session->forceShutdown();
				throw;
			}
		}

	protected:
		virtual void perform(const boost::shared_ptr<Session> &session) const = 0;
	};

	class Session::DataMessageJob : public Session::SyncJobBase {
	private:
		const boost::uint16_t m_messageId;
		const StreamBuffer m_payload;

	public:
		DataMessageJob(const boost::shared_ptr<Session> &session,
			boost::uint16_t messageId, StreamBuffer payload)
			: SyncJobBase(session)
			, m_messageId(messageId), m_payload(STD_MOVE(payload))
		{
		}

	protected:
		void perform(const boost::shared_ptr<Session> &session) const OVERRIDE {
			PROFILE_ME;

			LOG_POSEIDON_DEBUG("Dispatching message: messageId = ", m_messageId, ", payloadLen = ", m_payload.size());
			session->onSyncDataMessage(m_messageId, m_payload);

			const AUTO(keepAliveTimeout, MainConfig::get<boost::uint64_t>("cbpp_keep_alive_timeout", 30000));
			session->setTimeout(keepAliveTimeout);
		}
	};

	class Session::ControlMessageJob : public Session::SyncJobBase {
	private:
		const ControlCode m_controlCode;
		const boost::int64_t m_vintParam;
		const std::string m_stringParam;

	public:
		ControlMessageJob(const boost::shared_ptr<Session> &session,
			ControlCode controlCode, boost::int64_t vintParam, std::string stringParam)
			: SyncJobBase(session)
			, m_controlCode(controlCode), m_vintParam(vintParam), m_stringParam(stringParam)
		{
		}

	protected:
		void perform(const boost::shared_ptr<Session> &session) const OVERRIDE {
			PROFILE_ME;

			LOG_POSEIDON_DEBUG("Dispatching control message: controlCode = ", m_controlCode,
				", vintParam = ", m_vintParam, ", stringParam = ", m_stringParam);
			session->onSyncControlMessage(m_controlCode, m_vintParam, m_stringParam);

			const AUTO(keepAliveTimeout, MainConfig::get<boost::uint64_t>("cbpp_keep_alive_timeout", 30000));
			session->setTimeout(keepAliveTimeout);
		}
	};

	class Session::ErrorJob : public Session::SyncJobBase {
	private:
		const TcpSessionBase::DelayedShutdownGuard m_guard;

		mutable boost::uint16_t m_messageId;
		mutable StatusCode m_statusCode;
		mutable std::string m_reason;

	public:
		ErrorJob(const boost::shared_ptr<Session> &session,
			boost::uint16_t messageId, StatusCode statusCode, std::string reason)
			: SyncJobBase(session)
			, m_guard(session)
			, m_messageId(messageId), m_statusCode(statusCode), m_reason(STD_MOVE(reason))
		{
		}

	protected:
		void perform(const boost::shared_ptr<Session> &session) const OVERRIDE {
			PROFILE_ME;

			try {
				session->sendError(m_messageId, m_statusCode, STD_MOVE(m_reason));
			} catch(...){
				session->forceShutdown();
			}
		}
	};

	Session::Session(UniqueFile socket, boost::uint64_t maxRequestLength)
		: TcpSessionBase(STD_MOVE(socket))
		, m_maxRequestLength(maxRequestLength ? maxRequestLength : MainConfig::get<boost::uint64_t>("cbpp_max_request_length", 16384))
		, m_sizeTotal(0), m_messageId(0), m_payload()
	{
	}
	Session::~Session(){
	}

	void Session::onReadAvail(StreamBuffer data){
		PROFILE_ME;

		try {
			m_sizeTotal += data.size();
			if(m_sizeTotal > m_maxRequestLength){
				DEBUG_THROW(Exception, ST_REQUEST_TOO_LARGE);
			}

			Reader::putEncodedData(STD_MOVE(data));
		} catch(Exception &e){
			LOG_POSEIDON(Logger::SP_MAJOR | Logger::LV_INFO,
				"Cbpp::Exception thrown: statusCode = ", e.statusCode(), ", what = ", e.what());
			enqueueJob(boost::make_shared<ErrorJob>(
				virtualSharedFromThis<Session>(), Reader::getMessageId(), e.statusCode(), e.what()));
		}
	}
	void Session::onDataMessageHeader(boost::uint16_t messageId, boost::uint64_t /* payloadSize */){
		PROFILE_ME;

		m_messageId = messageId;
		m_payload.clear();
	}
	void Session::onDataMessagePayload(boost::uint64_t /* payloadOffset */, StreamBuffer payload){
		PROFILE_ME;

		m_payload.splice(payload);
	}
	bool Session::onDataMessageEnd(boost::uint64_t /* payloadSize */){
		PROFILE_ME;

		enqueueJob(boost::make_shared<DataMessageJob>(
			virtualSharedFromThis<Session>(), m_messageId, STD_MOVE(m_payload)));
		m_sizeTotal = 0;

		return true;
	}

	bool Session::onControlMessage(ControlCode controlCode, boost::int64_t vintParam, std::string stringParam){
		PROFILE_ME;

		enqueueJob(boost::make_shared<ControlMessageJob>(
			virtualSharedFromThis<Session>(), controlCode, vintParam, STD_MOVE(stringParam)));
		m_sizeTotal = 0;
		m_messageId = 0;
		m_payload.clear();

		return true;
	}

	long Session::onEncodedDataAvail(StreamBuffer encoded){
		PROFILE_ME;

		return TcpSessionBase::send(STD_MOVE(encoded));
	}

	void Session::onSyncControlMessage(ControlCode controlCode, boost::int64_t vintParam, const std::string &stringParam){
		PROFILE_ME;
		LOG_POSEIDON_DEBUG("Recevied control message from ", getRemoteInfo(),
			", controlCode = ", controlCode, ", vintParam = ", vintParam, ", stringParam = ", stringParam);

		switch(controlCode){
		case CTL_PING:
			LOG_POSEIDON_TRACE("Received ping from ", getRemoteInfo());
			send(ControlMessage(ControlMessage::ID, ST_PONG, stringParam));
			break;

		case CTL_SHUTDOWN:
			send(ControlMessage(ControlMessage::ID, ST_SHUTDOWN_REQUEST, stringParam));
			shutdownRead();
			shutdownWrite();
			break;

		case CTL_QUERY_MONO_CLOCK:
			send(ControlMessage(ControlMessage::ID, ST_MONOTONIC_CLOCK, boost::lexical_cast<std::string>(getFastMonoClock())));
			break;

		default:
			LOG_POSEIDON_WARNING("Unknown control code: ", controlCode);
			DEBUG_THROW(Exception, ST_UNKNOWN_CTL_CODE, SharedNts(stringParam));
		}
	}

	bool Session::send(boost::uint16_t messageId, StreamBuffer payload){
		PROFILE_ME;

		return Writer::putDataMessage(messageId, STD_MOVE(payload));
	}
	bool Session::sendError(boost::uint16_t messageId, StatusCode statusCode, std::string reason){
		PROFILE_ME;

		return Writer::putControlMessage(messageId, statusCode, STD_MOVE(reason));
	}
}

}
