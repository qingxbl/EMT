/*
 * EMT - Enhanced Memory Transfer (not emiria-tan)
 */

#ifndef __EMTCOMMON_H__
#define __EMTCOMMON_H__

#include <stdint.h>

#ifndef EXTERN_C
#ifdef __cplusplus
#define EXTERN_C extern "C"
#else
#define EXTERN_C
#endif // __cplusplus
#endif // EXTERN_C

#ifndef DECLSPEC_NOVTABLE
#if (_MSC_VER >= 1100) && defined(__cplusplus)
#define DECLSPEC_NOVTABLE __declspec(novtable)
#else
#define DECLSPEC_NOVTABLE
#endif
#endif // DECLSPEC_NOVTABLE

#ifdef __cplusplus
struct DECLSPEC_NOVTABLE IEMTUnknown
{
	virtual void destruct() = 0;
};

#define IMPL_IEMTUNKNOWN virtual void destruct() { delete this; }

struct IEMTUnknown_Delete
{
	void operator()(IEMTUnknown * p) const { p->destruct(); }
};
#endif // __cplusplus

#ifndef BEGIN_NAMESPACE_ANONYMOUS
#ifdef __cplusplus
#define BEGIN_NAMESPACE_ANONYMOUS namespace {
#else // !__cplusplus
#define BEGIN_NAMESPACE_ANONYMOUS
#endif // __cplusplus
#endif // !BEGIN_NAMESPACE_ANONYMOUS

#ifndef END_NAMESPACE_ANONYMOUS
#ifdef __cplusplus
#define END_NAMESPACE_ANONYMOUS }
#else // !__cplusplus
#define END_NAMESPACE_ANONYMOUS
#endif // __cplusplus
#endif // !END_NAMESPACE_ANONYMOUS

#ifndef EMT_PRIVATE
#ifdef __cplusplus
#define EMT_PRIVATE(TYPE, FIELD, NAME) TYPE##Private * NAME = (TYPE##Private *)&*this->FIELD
#else // !__cplusplus
#define EMT_PRIVATE(TYPE, FIELD, NAME)
#endif // __cplusplus
#define EMT_D(TYPE) EMT_PRIVATE(TYPE, d_ptr, d)
#endif // EMT_PRIVATE

#endif // __EMTCOMMON_H__
