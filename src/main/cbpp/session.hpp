// 这个文件是 Poseidon 服务器应用程序框架的一部分。
// Copyleft 2014 - 2015, LH_Mouse. All wrongs reserved.

#ifndef POSEIDON_CBPP_SESSION_HPP_
#define POSEIDON_CBPP_SESSION_HPP_

#include <cstddef>
#include <boost/type_traits/is_base_of.hpp>
#include <boost/utility/enable_if.hpp>
#include <boost/cstdint.hpp>
#include "../tcp_session_base.hpp"
#include "../stream_buffer.hpp"
#include "control_codes.hpp"
#include "status_codes.hpp"

namespace Poseidon {

namespace Cbpp {
	class MessageBase;

	class Session : public TcpSessionBase {
	private:
		enum State {
			S_PAYLOAD_LEN		= 0,
			S_EX_PAYLOAD_LEN	= 1,
			S_MESSAGE_ID		= 2,
			S_PAYLOAD			= 3,
		};

	private:
		class RequestJob;
		class ErrorJob;

	private:
		StreamBuffer m_received;

		boost::uint64_t m_sizeTotal;
		boost::uint64_t m_sizeExpecting;
		State m_state;

		boost::uint16_t m_messageId;
		boost::uint64_t m_payloadLen;

	public:
		explicit Session(UniqueFile socket);
		~Session();

	private:
		void onReadAvail(const void *data, std::size_t size) FINAL;

	protected:
		virtual void onRequest(boost::uint16_t messageId, const StreamBuffer &contents) = 0;
		virtual void onError(ControlCode controlCode, StatusCode statusCode, std::string reason);

	public:
		bool send(boost::uint16_t messageId, StreamBuffer contents, bool fin = false);

		template<class MessageT>
		typename boost::enable_if<boost::is_base_of<MessageBase, MessageT>, bool>::type
			send(const MessageT &contents, bool fin = false)
		{
			return send(MessageT::ID, StreamBuffer(contents), fin);
		}

		bool sendError(boost::uint16_t messageId, StatusCode statusCode, std::string reason, bool fin = false);
		bool sendError(boost::uint16_t messageId, StatusCode statusCode, bool fin = false){
			return sendError(messageId, statusCode, std::string(), fin);
		}
	};
}

}

#endif
