/* $OpenLDAP$ */
/*
 * Copyright 1998-2002 The OpenLDAP Foundation, All Rights Reserved.
 * COPYING RESTRICTIONS APPLY, see COPYRIGHT file
 */

/*
 * LDAPv3 Extended Operation Request
 *	ExtendedRequest ::= [APPLICATION 23] SEQUENCE {
 *		requestName      [0] LDAPOID,
 *		requestValue     [1] OCTET STRING OPTIONAL
 *	}
 *
 * LDAPv3 Extended Operation Response
 *	ExtendedResponse ::= [APPLICATION 24] SEQUENCE {
 *		COMPONENTS OF LDAPResult,
 *		responseName     [10] LDAPOID OPTIONAL,
 *		response         [11] OCTET STRING OPTIONAL
 *	}
 *
 */

#include "portable.h"

#include <stdio.h>
#include <ac/stdlib.h>

#include <ac/socket.h>
#include <ac/string.h>
#include <ac/time.h>

#include "ldap-int.h"

int
ldap_extended_operation(
	LDAP			*ld,
	LDAP_CONST char	*reqoid,
	struct berval	*reqdata,
	LDAPControl		**sctrls,
	LDAPControl		**cctrls,
	int				*msgidp )
{
	BerElement *ber;
	int rc;

#ifdef NEW_LOGGING
	LDAP_LOG (( "extended", LDAP_LEVEL_ENTRY, "ldap_extended_operation\n" ));
#else
	Debug( LDAP_DEBUG_TRACE, "ldap_extended_operation\n", 0, 0, 0 );
#endif

	assert( ld != NULL );
	assert( LDAP_VALID( ld ) );
	assert( reqoid != NULL || *reqoid == '\0' );
	assert( msgidp != NULL );

	/* must be version 3 (or greater) */
	if ( ld->ld_version < LDAP_VERSION3 ) {
		ld->ld_errno = LDAP_NOT_SUPPORTED;
		return( ld->ld_errno );
	}

	/* create a message to send */
	if ( (ber = ldap_alloc_ber_with_options( ld )) == NULL ) {
		ld->ld_errno = LDAP_NO_MEMORY;
		return( ld->ld_errno );
	}

	if ( reqdata != NULL ) {
		rc = ber_printf( ber, "{it{tstON}", /* '}' */
			++ld->ld_msgid, LDAP_REQ_EXTENDED,
			LDAP_TAG_EXOP_REQ_OID, reqoid,
			LDAP_TAG_EXOP_REQ_VALUE, reqdata );

	} else {
		rc = ber_printf( ber, "{it{tsN}", /* '}' */
			++ld->ld_msgid, LDAP_REQ_EXTENDED,
			LDAP_TAG_EXOP_REQ_OID, reqoid );
	}

	if( rc == -1 ) {
		ld->ld_errno = LDAP_ENCODING_ERROR;
		ber_free( ber, 1 );
		return( ld->ld_errno );
	}

	/* Put Server Controls */
	if( ldap_int_put_controls( ld, sctrls, ber ) != LDAP_SUCCESS ) {
		ber_free( ber, 1 );
		return ld->ld_errno;
	}

	if ( ber_printf( ber, /*{*/ "N}" ) == -1 ) {
		ld->ld_errno = LDAP_ENCODING_ERROR;
		ber_free( ber, 1 );
		return( ld->ld_errno );
	}

	/* send the message */
	*msgidp = ldap_send_initial_request( ld, LDAP_REQ_EXTENDED, NULL, ber );

	return( *msgidp < 0 ? ld->ld_errno : LDAP_SUCCESS );
}

int
ldap_extended_operation_s(
	LDAP			*ld,
	LDAP_CONST char	*reqoid,
	struct berval	*reqdata,
	LDAPControl		**sctrls,
	LDAPControl		**cctrls,
	char			**retoidp,
	struct berval	**retdatap )
{
    int     rc;
    int     msgid;
    LDAPMessage *res;

#ifdef NEW_LOGGING
	LDAP_LOG (( "extended", LDAP_LEVEL_ENTRY, "ldap_extended_operation_s\n" ));
#else
	Debug( LDAP_DEBUG_TRACE, "ldap_extended_operation_s\n", 0, 0, 0 );
#endif

	assert( ld != NULL );
	assert( LDAP_VALID( ld ) );
	assert( reqoid != NULL || *reqoid == '\0' );
	assert( retoidp != NULL || retdatap != NULL );

    rc = ldap_extended_operation( ld, reqoid, reqdata,
		sctrls, cctrls, &msgid );
        
    if ( rc != LDAP_SUCCESS ) {
        return( rc );
	}
 
    if ( ldap_result( ld, msgid, 1, (struct timeval *) NULL, &res ) == -1 ) {
        return( ld->ld_errno );
	}

	if ( retoidp != NULL ) *retoidp = NULL;
	if ( retdatap != NULL ) *retdatap = NULL;

	rc = ldap_parse_extended_result( ld, res, retoidp, retdatap, 0 );

	if( rc != LDAP_SUCCESS ) {
		ldap_msgfree( res );
		return rc;
	}

    return( ldap_result2error( ld, res, 1 ) );
}

/* Parse an extended result */
int
ldap_parse_extended_result (
	LDAP			*ld,
	LDAPMessage		*res,
	char			**retoidp,
	struct berval	**retdatap,
	int				freeit )
{
	BerElement *ber;
	ber_tag_t rc;
	ber_tag_t tag;
	ber_len_t len;
	struct berval *resdata;
	ber_int_t errcode;
	char *resoid;

	assert( ld != NULL );
	assert( LDAP_VALID( ld ) );
	assert( res != NULL );

#ifdef NEW_LOGGING
	LDAP_LOG (( "extended", LDAP_LEVEL_ENTRY, "ldap_parse_extended_result\n" ));
#else
	Debug( LDAP_DEBUG_TRACE, "ldap_parse_extended_result\n", 0, 0, 0 );
#endif

	if( ld->ld_version < LDAP_VERSION3 ) {
		ld->ld_errno = LDAP_NOT_SUPPORTED;
		return ld->ld_errno;
	}

	if( res->lm_msgtype != LDAP_RES_EXTENDED ) {
		ld->ld_errno = LDAP_PARAM_ERROR;
		return ld->ld_errno;
	}

	if( retoidp != NULL ) *retoidp = NULL;
	if( retdatap != NULL ) *retdatap = NULL;

	if ( ld->ld_error ) {
		LDAP_FREE( ld->ld_error );
		ld->ld_error = NULL;
	}

	if ( ld->ld_matched ) {
		LDAP_FREE( ld->ld_matched );
		ld->ld_matched = NULL;
	}

	ber = ber_dup( res->lm_ber );

	if ( ber == NULL ) {
		ld->ld_errno = LDAP_NO_MEMORY;
		return ld->ld_errno;
	}

	rc = ber_scanf( ber, "{iaa" /*}*/, &errcode,
		&ld->ld_matched, &ld->ld_error );

	if( rc == LBER_ERROR ) {
		ld->ld_errno = LDAP_DECODING_ERROR;
		ber_free( ber, 0 );
		return ld->ld_errno;
	}

	resoid = NULL;
	resdata = NULL;

	tag = ber_peek_tag( ber, &len );

	if( tag == LDAP_TAG_REFERRAL ) {
		/* skip over referral */
		if( ber_scanf( ber, "x" ) == LBER_ERROR ) {
			ld->ld_errno = LDAP_DECODING_ERROR;
			ber_free( ber, 0 );
			return ld->ld_errno;
		}

		tag = ber_peek_tag( ber, &len );
	}

	if( tag == LDAP_TAG_EXOP_RES_OID ) {
		/* we have a resoid */
		if( ber_scanf( ber, "a", &resoid ) == LBER_ERROR ) {
			ld->ld_errno = LDAP_DECODING_ERROR;
			ber_free( ber, 0 );
			return ld->ld_errno;
		}

		tag = ber_peek_tag( ber, &len );
	}

	if( tag == LDAP_TAG_EXOP_RES_VALUE ) {
		/* we have a resdata */
		if( ber_scanf( ber, "O", &resdata ) == LBER_ERROR ) {
			ld->ld_errno = LDAP_DECODING_ERROR;
			ber_free( ber, 0 );
			if( resoid != NULL ) LDAP_FREE( resoid );
			return ld->ld_errno;
		}
	}

	ber_free( ber, 0 );

	if( retoidp != NULL ) {
		*retoidp = resoid;
	} else {
		LDAP_FREE( resoid );
	}

	if( retdatap != NULL ) {
		*retdatap = resdata;
	} else {
		ber_bvfree( resdata );
	}

	ld->ld_errno = errcode;

	if( freeit ) {
		ldap_msgfree( res );
	}

	return LDAP_SUCCESS;
}


/* Parse an extended partial */
int
ldap_parse_extended_partial (
	LDAP			*ld,
	LDAPMessage		*res,
	char			**retoidp,
	struct berval	**retdatap,
	LDAPControl		***serverctrls,
	int				freeit )
{
	BerElement *ber;
	ber_tag_t rc;
	ber_tag_t tag;
	ber_len_t len;
	struct berval *resdata;
	char *resoid;

	assert( ld != NULL );
	assert( LDAP_VALID( ld ) );
	assert( res != NULL );

#ifdef NEW_LOGGING
	LDAP_LOG (( "extended", LDAP_LEVEL_ENTRY, 
		"ldap_parse_extended_partial\n" ));
#else
	Debug( LDAP_DEBUG_TRACE, "ldap_parse_extended_partial\n", 0, 0, 0 );
#endif

	if( ld->ld_version < LDAP_VERSION3 ) {
		ld->ld_errno = LDAP_NOT_SUPPORTED;
		return ld->ld_errno;
	}

	if( res->lm_msgtype != LDAP_RES_EXTENDED_PARTIAL ) {
		ld->ld_errno = LDAP_PARAM_ERROR;
		return ld->ld_errno;
	}

	if( retoidp != NULL ) *retoidp = NULL;
	if( retdatap != NULL ) *retdatap = NULL;

	ber = ber_dup( res->lm_ber );

	if ( ber == NULL ) {
		ld->ld_errno = LDAP_NO_MEMORY;
		return ld->ld_errno;
	}

	rc = ber_scanf( ber, "{" /*}*/ );

	if( rc == LBER_ERROR ) {
		ld->ld_errno = LDAP_DECODING_ERROR;
		ber_free( ber, 0 );
		return ld->ld_errno;
	}

	resoid = NULL;
	resdata = NULL;

	tag = ber_peek_tag( ber, &len );

	if( tag == LDAP_TAG_EXOP_RES_OID ) {
		/* we have a resoid */
		if( ber_scanf( ber, "a", &resoid ) == LBER_ERROR ) {
			ld->ld_errno = LDAP_DECODING_ERROR;
			ber_free( ber, 0 );
			return ld->ld_errno;
		}

		tag = ber_peek_tag( ber, &len );
	}

	if( tag == LDAP_TAG_EXOP_RES_VALUE ) {
		/* we have a resdata */
		if( ber_scanf( ber, "O", &resdata ) == LBER_ERROR ) {
			ld->ld_errno = LDAP_DECODING_ERROR;
			ber_free( ber, 0 );
			if( resoid != NULL ) LDAP_FREE( resoid );
			return ld->ld_errno;
		}
	}

	if ( serverctrls == NULL ) {
		rc = LDAP_SUCCESS;
		goto free_and_return;
	}

	if ( ber_scanf( ber, /*{*/ "}" ) == LBER_ERROR ) {
		rc = LDAP_DECODING_ERROR;
		goto free_and_return;
	}

	rc = ldap_int_get_controls( ber, serverctrls );

free_and_return:
	ber_free( ber, 0 );

	if( retoidp != NULL ) {
		*retoidp = resoid;
	} else {
		LDAP_FREE( resoid );
	}

	if( retdatap != NULL ) {
		*retdatap = resdata;
	} else {
		ber_bvfree( resdata );
	}

	if( freeit ) {
		ldap_msgfree( res );
	}

	return LDAP_SUCCESS;
}
