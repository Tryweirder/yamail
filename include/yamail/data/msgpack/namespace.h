#ifndef _YAMAIL_DATA_MSGPACK_NAMESPACE_H_
#define _YAMAIL_DATA_MSGPACK_NAMESPACE_H_
#include <yamail/config.h>
#include <yamail/data/namespace.h>

#define YAMAIL_NS_DATA_MSGPACK msgpack
#define YAMAIL_FQNS_DATA_MSGPACK YAMAIL_FQNS_DATA::YAMAIL_NS_DATA_MSGPACK

#define YAMAIL_NS_DATA_MSGPACK_BEGIN namespace YAMAIL_NS_DATA_MSGPACK {
#define YAMAIL_NS_DATA_MSGPACK_END }

#define YAMAIL_FQNS_DATA_MSGPACK_BEGIN \
  YAMAIL_NS_BEGIN YAMAIL_NS_DATA_BEGIN YAMAIL_NS_DATA_MSGPACK_BEGIN

#define YAMAIL_FQNS_DATA_MSGPACK_END \
  YAMAIL_NS_DATA_MSGPACK_END YAMAIL_NS_DATA_END YAMAIL_NS_END

#endif // _YAMAIL_DATA_MSGPACK_NAMESPACE_H_
