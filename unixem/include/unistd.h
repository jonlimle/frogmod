/* /////////////////////////////////////////////////////////////////////////////
 * File:    unistd.h
 *
 * Purpose: Declaration of various UNIX standard functions.
 *
 * Created: 1st November 2003
 * Updated: 13th May 2008
 *
 * Home:    http://synesis.com.au/software/
 *
 * Copyright (c) 2003-2008, Matthew Wilson and Synesis Software
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 * - Neither the names of Matthew Wilson and Synesis Software nor the names of
 *   any contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * ////////////////////////////////////////////////////////////////////////// */


/** \file unistd.h
 *
 * Contains standard limits and declarations.
 */

#ifndef SYNSOFT_UNIXEM_INCL_H_UNISTD
#define SYNSOFT_UNIXEM_INCL_H_UNISTD

#ifndef UNIXEM_DOCUMENTATION_SKIP_SECTION
# define SYNSOFT_UNIXEM_VER_H_UNISTD_MAJOR      2
# define SYNSOFT_UNIXEM_VER_H_UNISTD_MINOR      6
# define SYNSOFT_UNIXEM_VER_H_UNISTD_REVISION   3
# define SYNSOFT_UNIXEM_VER_H_UNISTD_EDIT       36
#endif /* !UNIXEM_DOCUMENTATION_SKIP_SECTION */

/* /////////////////////////////////////////////////////////////////////////////
 * Includes
 */

#include <stddef.h>     /* for size_t */
#include <sys/types.h>  /* for mode_t */

/* Some of the functions declared here (and defined in unistd.c) may be
 * provided by some Win32 compilers. So we discriminate for support here,
 * and exclude definitions as appropriate.
 */

#if defined(__BORLANDC__)
# include <dir.h>
# define UNIXEM_chdir_PROVIDED_BY_COMPILER
# define UNIXEM_getcwd_PROVIDED_BY_COMPILER
# define UNIXEM_mkdir_PROVIDED_BY_COMPILER
# define UNIXEM_rmdir_PROVIDED_BY_COMPILER
#elif defined(__DMC__)
# include <direct.h>
# define UNIXEM_chdir_PROVIDED_BY_COMPILER
# define UNIXEM_close_PROVIDED_BY_COMPILER
# define UNIXEM_getcwd_PROVIDED_BY_COMPILER
# define UNIXEM_pid_t_PROVIDED_BY_COMPILER
#elif defined(__GNUC__)
# include <io.h>
# define UNIXEM_chdir_PROVIDED_BY_COMPILER
# define UNIXEM_chmod_PROVIDED_BY_COMPILER
# define UNIXEM_getcwd_PROVIDED_BY_COMPILER
# define UNIXEM_mkdir_PROVIDED_BY_COMPILER
# ifndef _NO_OLDNAMES
#  define UNIXEM_pid_t_PROVIDED_BY_COMPILER
# endif /* !_NO_OLDNAMES */
# define UNIXEM_rmdir_PROVIDED_BY_COMPILER
#elif defined(__INTEL_COMPILER)
# if defined(_WIN32) && \
     !defined(__STDC__)
#  include <direct.h>
#  define UNIXEM_chdir_PROVIDED_BY_COMPILER
#  define UNIXEM_getcwd_PROVIDED_BY_COMPILER
#  define UNIXEM_mkdir_PROVIDED_BY_COMPILER
#  define UNIXEM_rmdir_PROVIDED_BY_COMPILER
# endif /* !__STDC__ */
#elif defined(__MWERKS__)
# define UNIXEM_mkdir_PROVIDED_BY_COMPILER
#elif defined(__WATCOMC__)
# define UNIXEM_chdir_PROVIDED_BY_COMPILER
# define UNIXEM_getcwd_PROVIDED_BY_COMPILER
# define UNIXEM_mkdir_PROVIDED_BY_COMPILER
# define UNIXEM_pid_t_PROVIDED_BY_COMPILER
# define UNIXEM_rmdir_PROVIDED_BY_COMPILER
#elif defined(_MSC_VER)
# if !defined(__STDC__)
#  include <direct.h>
#  define UNIXEM_chdir_PROVIDED_BY_COMPILER
#  define UNIXEM_getcwd_PROVIDED_BY_COMPILER
#  define UNIXEM_mkdir_PROVIDED_BY_COMPILER
#  define UNIXEM_rmdir_PROVIDED_BY_COMPILER
#  include <stdio.h>
#  define UNIXEM_unlink_PROVIDED_BY_COMPILER
# endif /* !__STDC__ */
#else
# error Compiler not discriminated
#endif /* compiler */


#if defined(_MSC_VER) && \
    !defined(__STDC__)
# define UNIXEM_UNISTD_INCLUDING_MS_DIRECT_H
#endif /* compiler */

/* ////////////////////////////////////////////////////////////////////////// */

/** \weakgroup unixem Synesis Software UNIX Emulation for Win32
 * \brief The UNIX emulation library
 */

/** \weakgroup unixem_unistd unistd.h
 * \ingroup UNIXem unixem
 * \brief Standard limits and declarations
 * @{
 */

/* ////////////////////////////////////////////////////////////////////////// */

#ifndef _WIN32
# error This file is only currently defined for compilation on Win32 systems
#endif /* _WIN32 */

/* /////////////////////////////////////////////////////////////////////////////
 * Constants and definitions
 */

//#ifndef PATH_MAX
//# define PATH_MAX   (260)   /*!< \brief The maximum number of characters (including null terminator) in a directory entry name */
//#endif /* !PATH_MAX */

enum
{
        _PC_LINK_MAX                    /*!< The maximum number of links to the file. */
    ,   _PC_MAX_CANON                   /*!< Maximum number of bytes in canonical input line. Applicable only to terminal devices. */
    ,   _PC_MAX_INPUT                   /*!< Maximum number of bytes allowed in an input queue. Applicable only to terminal devices. */
    ,   _PC_NAME_MAX                    /*!< Maximum number of bytes in a file name, not including a nul terminator. This number can range from 14 through 255. This value is applicable only to a directory file. */
    ,   _PC_PATH_MAX                    /*!< Maximum number of bytes in a path name, including a nul terminator. */
 
    ,   _PC_PIPE_BUF                    /*!< Maximum number of bytes guaranteed to be written atomically. This value is applicable only to a first-in-first-out (FIFO). */
    ,   _PC_CHOWN_RESTRICTED            /*!< Returns 0 if the use of the chown subroutine is restricted to a process with appropriate privileges, and if the chown subroutine is restricted to changing the group ID of a file only to the effective group ID of the process or to one of its supplementary group IDs. */
    ,   _PC_NO_TRUNC                    /*!< Returns 0 if long component names are truncated. This value is applicable only to a directory file. */
    ,   _PC_VDISABLE                    /*!< This is always 0. No disabling character is defined. This value is applicable only to a terminal device. */
    ,   _PC_AIX_DISK_PARTITION          /*!< Determines the physical partition size of the disk. 
Note:
The _PC_AIX_DISK_PARTITION variable is available only to the root user. */
    ,   _PC_AIX_DISK_SIZE               /*!< Determines the disk size in megabytes. 
Note:
The _PC_AIX_DISK_SIZE variable is available only to the root user.
Note:
The _PC_FILESIZEBITS and PC_SYNC_IO flags apply to AIX 4.3 and later releases. */
    ,   _PC_FILESIZEBITS                /*!< Returns the minimum number of bits required to hold the file system's maximum file size as a signed integer. The smallest value returned is 32. */
    ,   _PC_SYNC_IO                     /*!< Returns -1 if the file system does not support the Synchronized Input and Output option. Any value other than -1 is returned if the file system supports the option. */

#ifndef UNIXEM_DOCUMENTATION_SKIP_SECTION
# define _PC_LINK_MAX               _PC_LINK_MAX
# define _PC_MAX_CANON              _PC_MAX_CANON
# define _PC_MAX_INPUT              _PC_MAX_INPUT
# define _PC_NAME_MAX               _PC_NAME_MAX
# define _PC_PATH_MAX               _PC_PATH_MAX
# define _PC_PIPE_BUF               _PC_PIPE_BUF
# define _PC_CHOWN_RESTRICTED       _PC_CHOWN_RESTRICTED
# define _PC_NO_TRUNC               _PC_NO_TRUNC
# define _PC_VDISABLE               _PC_VDISABLE
# define _PC_AIX_DISK_PARTITION     _PC_AIX_DISK_PARTITION
# define _PC_AIX_DISK_SIZE          _PC_AIX_DISK_SIZE
# define _PC_FILESIZEBITS           _PC_FILESIZEBITS
# define _PC_SYNC_IO                _PC_SYNC_IO
#endif /* !UNIXEM_DOCUMENTATION_SKIP_SECTION */
};

/* /////////////////////////////////////////////////////////////////////////////
 * Typedefs
 */

#if !defined(UNIXEM_pid_t_PROVIDED_BY_COMPILER) && \
    !defined(pid_t) && \
    !(  defined(_SCHED_H) && \
        defined(PTW32_DLLPORT))
# define UNIXEM_pid_t_DEFINED
typedef int         pid_t;
#endif /* !UNIXEM_pid_t_PROVIDED_BY_COMPILER && !pid_t */

/* /////////////////////////////////////////////////////////////////////////////
 * API functions
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** \brief Creates a hardlink.
 *
 * This function creates a link from \c originalFile to \c linkName.
 *
 * \param originalFile Path of the original file
 * \param linkName Path of the link
 *
 * \return O on success, or -1 if there is an error
 *
 * \note Hardlink support is only available on Windows 2000 and later, and only
 *        works within a single drive.
 */
int link(char const* originalFile, char const* linkName);

/** \brief Unlinks a file or directory
 *
 * \param path The path of the file or directory to unlink
 *
 * \return O on success, or -1 if there is an error
 */
#ifndef UNIXEM_unlink_PROVIDED_BY_COMPILER
int unlink(char const* path);
#endif /* !UNIXEM_unlink_PROVIDED_BY_COMPILER */


/** \brief Change the current working directory.
 *
 * This function changes the current working directory to the directory
 * specified by dirName. dirName must refer to an existing directory.
 *
 * \param dirName Path of new working directory
 * \return O on success, or -1 if there is an error
 */
#ifndef UNIXEM_chdir_PROVIDED_BY_COMPILER
int chdir(char const* dirName);
#endif /* !UNIXEM_chdir_PROVIDED_BY_COMPILER */


/** \brief Get the current working directory
 *
 * This function gets the full path of the current working directory
 * and stores it in buffer.
 *
 * \param buffer Storage location for the current working directory
 * \param max_len Maximum length of path (in characters)
 * \return buffer on success, or NULL to indicate error.
 */
#ifndef UNIXEM_getcwd_PROVIDED_BY_COMPILER
char *getcwd(char *buffer, size_t max_len);
#endif /* !UNIXEM_getcwd_PROVIDED_BY_COMPILER */


/** \brief Creates the given directory
 *
 * This function creates the named directory.
 *
 * \param dirName Path of directory to remove
 * \param mode The access permissions of the directory
 *
 * \return O on success, or -1 if there is an error
 */
#ifndef UNIXEM_mkdir_PROVIDED_BY_COMPILER
int mkdir(char const* dirName, unsigned mode);
#endif /* !UNIXEM_mkdir_PROVIDED_BY_COMPILER */


/** \brief Removes the given directory
 *
 * This function removes the named directory.
 *
 * \param dirName Path of directory to remove
 * \return O on success, or -1 if there is an error
 */
#ifndef UNIXEM_rmdir_PROVIDED_BY_COMPILER
int rmdir(char const* dirName);
#endif /* !UNIXEM_rmdir_PROVIDED_BY_COMPILER */

/** \brief Closes a file
 *
 * \param handle The handle of the file to be closed
 * \return 0 on success, or -1 if there is an error
 */
#ifndef UNIXEM_close_PROVIDED_BY_COMPILER
int close(int handle);
#endif /* !UNIXEM_close_PROVIDED_BY_COMPILER */

/* * \brief Creates a pipe
 *
 * \param handles An array of two handles. handles[0] will be set to the
 * read stream. handles[1] will be set to the write stream
 * \return 0 on success, or -1 if there is an error
 */
/* int pipe(int handles[2]); */

/** \brief Returns the size, in bytes, of the page size
 */
int getpagesize(void);

/** \brief Provides access to various system limits not available at compile time
 */
long pathconf(char const *path, int name);


/** \brief Turns \c path into a fully qualified path, resolving all symbolic 
 * links, multiple /, /./ and /../
 *
 * \param path The relative path to be converted into absolute form
 * \param resolvedPath Pointer to a buffer to receive the path. This must contain
 *  sufficient storage for a valid path
 */
char *realpath(char const *path, char resolvedPath[]);

/** \brief Suspends execution for the given internal
 *
 * \param microSeconds The number of microseconds in the sleep interval
 */
int usleep(unsigned long microSeconds);


/** \brief Returns the current process identifier
 */
pid_t getpid(void);


/** \brief Returns the host name for the current machine
 *
 * \param name Pointer to an array of characters to receive the results
 * \param cchName Number of characters available in the buffer
 *
 * \retval 0 the operation completed successfully
 * \retval -1 the operation failed
 */
#ifdef UNIXEM_DOCUMENTATION_SKIP_SECTION
int gethostname(char* name, size_t cchName);
#else /* ? UNIXEM_DOCUMENTATION_SKIP_SECTION */
int unixem_gethostname(char* name, size_t cchName);
# ifdef _WINSOCKAPI_
  /* already included, so we can redefine and do whatever we want/need */
# else /* ? _WINSOCKAPI_ */
#  define _WINSOCKAPI_ /* This will stop anyone else including it also */.
# endif /* _WINSOCKAPI_ */
# ifdef _WINSOCK2API_
# else /* ? _WINSOCK2API_ */
#  define _WINSOCK2API_
# endif /* _WINSOCK2API_ */


# ifdef __cplusplus
inline int gethostname(char* name, size_t cchName)
{
    return unixem_gethostname(name, cchName);
}
# endif /* __cplusplus */

# define gethostname(name, cchName)		unixem_gethostname(name, cchName)
#endif /* UNIXEM_DOCUMENTATION_SKIP_SECTION */

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

/* ////////////////////////////////////////////////////////////////////////// */

/** @} // end of group unixem_unistd */

/* ////////////////////////////////////////////////////////////////////////// */

#endif /* SYNSOFT_UNIXEM_INCL_H_UNISTD */

/* ////////////////////////////////////////////////////////////////////////// */
