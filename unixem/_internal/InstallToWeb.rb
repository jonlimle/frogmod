################################################################################
# File:		InstallToWeb.rb
#
# Purpose:	Various utility functions for handling dates and versions
#
# Imports:  openrj, recls, synsoft/cmdline, synsoft/srcutil, UNPrompt
#
# Created:	17th February 2005
# Updated:  24th February 2008
#
# Home:		http://synesis.com.au/software
#
# Copyright (c) 2005-2008, Matthew Wilson and Synesis Software
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# - Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
# - Neither the name(s) of Matthew Wilson and Synesis Software nor the names of
#   any contributors may be used to endorse or promote products derived from
#   this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.
#
################################################################################


################################################################################
# Version Info

SYNSOFT_VER_RB_INSTALLTOWEB_MAJOR		=	1
SYNSOFT_VER_RB_INSTALLTOWEB_MINOR		=	0
SYNSOFT_VER_RB_INSTALLTOWEB_REVISION	=	6
SYNSOFT_VER_RB_INSTALLTOWEB_EDIT		=	6

################################################################################
# Imports

require 'openrj'
require 'recls'
require 'synsoft/cmdline'
require 'synsoft/srcutil'
#require 'UNPrompt'

#abort if not puts Recls::usage()
#abort if not puts UNPrompt::usage()

# This script does the following tasks:

# 0. Check that the UNIXEM_VER environment variable exists, or has been passed in on the command-line
# 1. Check that the zip & md5 files exist
# 2. Check that the software/index.html files exists and is writeable
# 3. Check if the section for the current version does not exist in the HTML
# 4. Check that the files do not exist in the download directory
# 5. Check that the files do not exist in the directory 'online'
# 6. Write the section into the file
# 7. Copies the files into the destination

################################################################################
# Classes

class Info

		def initialize(fileName, fileExt, source)
				@fileName		=	fileName
				@fileExt		=	fileExt
				@source			=	source
				@destination	=	nil
				@bUpToDate		=	nil
		end

		def relative(dir)

				if dir.length
						last = dir[(dir.length) -1 ... dir.length]
						case	last
						when "\\", "/"
						else
								raise "Info::relative(): directory was not terminated with a path separator; dir: #{dir}"
						end
				end

				return dir + fileName + '.' + fileExt
				
		end

		attr_reader :fileName, :fileExt, :bUpToDate, :source, :destination
		attr_writer :bUpToDate, :destination

end # class Info
		
def usage

		puts "InstallToWeb.rb [-v] [{-E<opt0-name>=<opt0-value> . . . -E<opt(N-1)-name>=<opt(N-1)-value>}] <release-notes-file>"

		exit(1)

end # usage

################################################################################
# Command-line processing

arguments 	= 	CmdLine::Arguments::new(ARGV)

values		=	arguments.values
options		=	arguments.options

################################################################################
# Main processing

bReportOnly			=	nil
bVerbose			=	nil

releaseNotesFile	=	values.shift if values.length

options.each \
{ |option|
		case	option
		when "?"
				usage()
		when "v"
				bVerbose = 1
		else
				if option[0 ... 1] == "E"
						option = option[1 ... option.length]
						if option =~ /(\w+)=(.*?)$/
								ENV[$1] = $2
						else
								abort "Invalid option \"-E#{option}\"; should be \"-E<env-var-name>=<env-var-value\""
						end
				end
		end
}


if not releaseNotesFile
		abort "Must specify the release notes file"
end

# 0. Check that the UNIXEM_VER environment variable exists, or has been passed in on the command-line

PROJECT_NAME		=	'unixem'
PROJECT_VER_NAME	=	'UNIXEM_VER'
PROJECT_CONFIG_FILE	=	'unixem-deploy.rj'

if ENV.has_key?(PROJECT_VER_NAME)
		projVer	=	ENV[PROJECT_VER_NAME]
else
		abort PROJECT_VER_NAME + " must be defined"
end

puts "Processing deployment for #{PROJECT_NAME} version #{projVer}"

# 0.b Open the configuration file

if bVerbose
		puts "  Opening the configuration file: " + PROJECT_CONFIG_FILE
end

db = OpenRJ::FileDatabase::new(PROJECT_CONFIG_FILE, OpenRJ::ELIDE_BLANK_RECORDS)

r = db[0]

# 1. Check that the zip & md5 files exist

if bVerbose
		puts "  Verifying the existence of the .zip and .md5 files"
end

baseName = PROJECT_NAME + '-' + String(projVer)

fs = Recls::FileSearch::new(r['ReadyBinariesDir'], baseName + ".*", Recls::FILES)

feZip	=	fs.select { |fe| fe.fileExt == "zip" }
feMD5	=	fs.select { |fe| fe.fileExt == "md5" }

[[feZip, baseName + ".zip"], [feMD5, baseName + ".md5"] ].each { |pair| abort "The source file \"#{pair[1]}\" does not exist - aborting" if not pair[0][0] }

feZip = feZip[0]
feMD5 = feMD5[0]

if bVerbose
		puts "    Zip file: " + String(feZip)
		puts "    MD5 file: " + String(feMD5)
end

infos = [ Info::new(baseName, 'zip', feZip), Info::new(baseName, 'md5', feMD5) ]

# 2. Check that the software/index.html files exists and is writeable

if bVerbose
		puts "  Verifying the existence of the HTML file, and that it is writable"
end

webPage	=	r['WebPage']

if not File::exist?(webPage)
		abort "WebPage \"#{webPage}\" does not exist"
elsif not File::stat(webPage).writable?()
		puts "WebPage \"#{webPage}\" exists but is not writeable"
#		puts "Attempting to check it out from SourceSafe"
else
#		puts "Web-page: " + webPage
end

# 3. Check if the section for the current version does not exist in the HTML

if bVerbose
		puts "  Processing the HTML file, to detect insertion point"
end

f = File::open(webPage)
lines = f.readlines()
f.close()

insertionPointBegin	=	-1
insertionPointEnd	=	-1
insertionPointBeginVer	=	-1
insertionPointEndVer	=	-1

INSERTIONPOINT_BEGIN		=	"#{PROJECT_NAME}-InsertionPoint-Begin"
INSERTIONPOINT_END			=	"#{PROJECT_NAME}-InsertionPoint-End"
INSERTIONPOINT_BEGIN_VER	=	"#{PROJECT_NAME}-InsertionPoint-Begin:#{projVer}"
INSERTIONPOINT_END_VER		=	"#{PROJECT_NAME}-InsertionPoint-End:#{projVer}"

lines.each_with_index \
{ |line, lineNum_|
		lineNum = lineNum_ + 1

		# InsertionPoint-Begin
		if line =~ /<!-- #{INSERTIONPOINT_BEGIN} -->/
#				puts "#{INSERTIONPOINT_BEGIN} at line #{lineNum}"
				if insertionPointBegin >= 0
						abort "#{webPage}(#{lineNum}): #{INSERTIONPOINT_BEGIN} already defined at line #{insertionPointBegin}"
				end
				insertionPointBegin	= lineNum_
		# InsertionPoint-End
		elsif line =~ /\<\!-- #{INSERTIONPOINT_END} --\>/
#				puts "#{INSERTIONPOINT_END} at line #{lineNum}"
				if insertionPointEnd >= 0
						abort "#{webPage}(#{lineNum}): #{INSERTIONPOINT_END} already defined at line #{insertionPointEnd}"
				elsif insertionPointBegin < 0
						abort "#{webPage}(#{lineNum}): #{INSERTIONPOINT_END} before #{INSERTIONPOINT_BEGIN}"
				end
				insertionPointEnd	= lineNum_
		# InsertionPoint-Begin:$(projVer)
		elsif line =~ /<!-- #{INSERTIONPOINT_BEGIN_VER} -->/
#				puts "#{INSERTIONPOINT_BEGIN_VER} at line #{lineNum}"
				if insertionPointBeginVer >= 0
						abort "#{webPage}(#{lineNum}): #{INSERTIONPOINT_BEGIN_VER} already defined at line #{insertionPointBegin}"
				end
				insertionPointBeginVer	= lineNum_
		# InsertionPoint-End:$(projVer)
		elsif line =~ /<!-- #{INSERTIONPOINT_END_VER} -->/
#				puts "#{INSERTIONPOINT_END_VER} at line #{lineNum}"
				if insertionPointEndVer >= 0
						abort "#{webPage}(#{lineNum}): #{INSERTIONPOINT_END_VER} already defined at line #{insertionPointEnd}"
				elsif insertionPointBeginVer < 0
						abort "#{webPage}(#{lineNum}): #{INSERTIONPOINT_END_VER} before #{INSERTIONPOINT_BEGIN_VER}"
				end
				insertionPointEndVer	= lineNum_
		end
}

if insertionPointBegin < 0
		abort "Failed to find #{INSERTIONPOINT_BEGIN} in #{webPage}"
end
if insertionPointEnd < 0
		abort "Failed to find #{INSERTIONPOINT_END} in #{webPage}"
end

if ((insertionPointBeginVer + 0.5) * (insertionPointEndVer + 0.5)) < 0
		abort "Found #{INSERTIONPOINT_BEGIN_VER} and not #{INSERTIONPOINT_BEGIN_VER}, or vice versa"
end

# 4. Check that the files do not exist in the download directory

if bVerbose
		puts "  Detecting presence of files in the upload directory"
end

webUploadDir	=	r['WebUploadDir']
if webUploadDir.length > 0 and webUploadDir[webUploadDir.length - 1 ... webUploadDir.length] != "\\"
		webUploadDir = webUploadDir + "\\"
end

fs = Recls::FileSearch::new(webUploadDir, baseName + ".*", Recls::FILES)

feZipDest	=	fs.select { |fe| fe.fileExt.downcase == "zip" }
feMD5Dest	=	fs.select { |fe| fe.fileExt.downcase == "md5" }

[[feZipDest, "zip"], [feMD5Dest, "md5"] ].each \
{ |pair|
		fe = pair[0][0]
		if fe and fe.fileExt != pair[1]
				abort "The destination file \"#{fe}\" has invalid case - aborting"
		end
}

#[[feZipDest, baseName + ".zip"], [feMD5Dest, baseName + ".md5"] ].each { |pair| puts "The destination file \"#{webUploadDir}\\#{pair[1]}\" does not exist" if not pair[0][0] }

feZipDest = feZipDest[0] if feZipDest
feMD5Dest = feMD5Dest[0] if feMD5Dest

infos[0].destination = feZipDest
infos[1].destination = feMD5Dest

#[ [feZip, feZipDest], [feMD5, feMD5Dest] ].each_with_index \
infos.each \
{ |info, i|
		src		=	info.source
		dest	=	info.destination
		if dest
				if src.modificationTime == dest.modificationTime
						if bVerbose
								puts "    The destination \"#{dest}\" is up to date"
						end
						info.bUpToDate = 1
				else
						if dest.isReadOnly
								abort "    The destination \"#{dest}\" is read-only - aborting!"
						elsif src.modificationTime < dest.modificationTime
								abort "    The destination \"#{dest}\" is NEWER than the source \"#{src}\" - aborting!"
						elsif src.modificationTime > dest.modificationTime
								if bVerbose
										puts "    " + webUploadDir + src.file + " exists, but is out of date"
								end
#						elsif src.size != dest.size
#								abort "    Different sizes; src: #{src.size}; dest: #{dest.size}"
						else
								if bVerbose
										puts "    #{dest} exists"
								end
						end
				end
		else
				if bVerbose
						puts "    " + webUploadDir + src.file + " does not exist"
				end
		end
}

# 5. Check that the files do not exist in the directory 'online'

if bVerbose
		puts "  Detecting presence of files in the online directory"
end

#host		=	r['FTPHost']
#userName	=	r['FTPUserName']

#prompt = UNPrompt::Prompt::new("FTP Access", "Enter Username / Password for [#{host}: #{userName}]", UNPrompt::PASSWORD)

#fs = Recls::FtpSearch::new(host, userName, prompt.password, r['FTPDownloadDir'], '#{baseName}.zip;#{baseName}.md5', Recls::FILES)
#
#fs.each \
#{ |fe|
#		case fe.fileExt
#		when "md5", "zip"
#				puts "Found: #{fe}"
#		end
#}

# 6. Prepare the html file contents, including the release notes

if bVerbose
		puts "  Preparing the HTML file"
end

f = File::open(releaseNotesFile)
releaseNotes = f.readlines()
f.close

newLines	=	[]

if insertionPointEndVer < 0
		lastInsertionPointEnd	=	insertionPointBegin + 1
else
		lastInsertionPointEnd	=	insertionPointEndVer
end

newLines	=	newLines + lines[0 ... insertionPointBegin + 1]

wwwDownloadDir	=	r['WWWDownloadDir']
if wwwDownloadDir.length > 0 and wwwDownloadDir[wwwDownloadDir.length - 1 ... wwwDownloadDir.length] != "/"
		wwwDownloadDir = wwwDownloadDir + "/"
end

newLines	<< "  <!-- #{INSERTIONPOINT_BEGIN_VER} -->" << "\n"
newLines	<< "  <tr>" << "\n"
newLines	<< "   <td valign = \"top\">" << "\n"
newLines	<< "    <b>" + SrcUtil::currentDate() + "</b>" << "\n"
newLines	<< "   </td>" << "\n"
newLines	<< "   <td valign = \"top\">" << "\n"
newLines	<< "    <b>#{projVer}</b>" << "\n"
newLines	<< "   </td>" << "\n"
newLines	<< "   <td valign = \"top\">" << "\n"
newLines	<< "    <a href = \"#{wwwDownloadDir}#{baseName}.zip\">#{baseName}.zip</a> (#{feZip.size} bytes)" << "\n"
newLines	<< "   </td>" << "\n"
newLines	<< "   <td valign = \"top\">" << "\n"
newLines	<< "    <a href = \"#{wwwDownloadDir}#{baseName}.md5\">#{baseName}.md5</a> (#{feMD5.size} bytes)" << "\n"
newLines	<< "    <br />" << "\n"
f = File::new(feMD5.path)
md5Lines	=	f.readlines()
md5 = md5Lines[0]
if md5 =~ /^(.*)$/
		md5 = $1
end
f.close()
newLines	<< "    " + md5
newLines	<< "   </td>" << "\n"
newLines	<< "   <td valign = \"top\">" << "\n"
releaseNotes.each { |line| newLines	<< "    " + line }
newLines	<< "   </td>" << "\n"
newLines	<< "  </tr>" << "\n"
newLines	<< "  <!-- #{INSERTIONPOINT_END_VER} -->" << "\n"

newLines	=	newLines + lines[lastInsertionPointEnd + 1 ... lines.length]

#puts "Output:"
#newLines.each { |line| puts line }

# 7. Copies the distribution files into the destination

if bVerbose
		puts "  Copying the distribution files into the upload directory"
end

infos.each \
{ |info|
		if info.bUpToDate
				next
		end
		if bVerbose
				puts "      copying #{info.source.path} => " + info.relative(webUploadDir)
		end
#		File::copy(info.source.path, info.relative(webUploadDir))
		system("copy \"#{info.source.path}\" \"" + info.relative(webUploadDir))
}


# 8. Update the HTML file

if bVerbose
		puts "  Writing the updated HTML file: " + r['WebPage']
end

f = File::new(r['WebPage'], "w")
newLines.each { |line| f << line }
f.close()


