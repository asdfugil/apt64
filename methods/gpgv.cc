#include <apt-pkg/error.h>
#include <apt-pkg/acquire-method.h>
#include <apt-pkg/strutl.h>

#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <iostream>

#define GNUPGPREFIX "[GNUPG:]"
#define GNUPGBADSIG "[GNUPG:] BADSIG"
#define GNUPGNOPUBKEY "[GNUPG:] NO_PUBKEY"
#define GNUPGVALIDSIG "[GNUPG:] VALIDSIG"

class GPGVMethod : public pkgAcqMethod
{
   private:
   const char *VerifyGetSigners(const char *file, const char *outfile,
				vector<string> &GoodSigners, vector<string> &BadSigners,
				vector<string> &NoPubKeySigners);
   
   protected:
   virtual bool Fetch(FetchItem *Itm);
   
   public:
   
   GPGVMethod() : pkgAcqMethod("1.0",SingleInstance | SendConfig) {};
};

const char *GPGVMethod::VerifyGetSigners(const char *file, const char *outfile,
					 vector<string> &GoodSigners,
					 vector<string> &BadSigners,
					 vector<string> &NoPubKeySigners)
{
   if (_config->FindB("Debug::Acquire::gpgv", false))
   {
      std::cerr << "inside VerifyGetSigners" << std::endl;
   }
   pid_t pid;
   int fd[2];
   FILE *pipein;
   int status;
   struct stat buff;
   string gpgvpath = _config->Find("Dir::Bin::gpg", "/usr/bin/gpgv");
   string pubringpath = _config->Find("Apt::GPGV::TrustedKeyring", "/etc/apt/trusted.gpg");
   if (_config->FindB("Debug::Acquire::gpgv", false))
   {
      std::cerr << "gpgv path: " << gpgvpath << std::endl;
      std::cerr << "Keyring path: " << pubringpath << std::endl;
   }

   if (stat(pubringpath.c_str(), &buff) != 0)
      return (string("Couldn't access keyring: ") + strerror(errno)).c_str();

   if (pipe(fd) < 0)
   {
      return "Couldn't create pipe";
   }

   pid = fork();
   if (pid < 0)
   {
      return (string("Couldn't spawn new process") + strerror(errno)).c_str();
   }
   else if (pid == 0)
   {
      const char *Args[400];
      unsigned int i = 0;

      Args[i++] = gpgvpath.c_str();
      Args[i++] = "--status-fd";
      Args[i++] = "3";
      Args[i++] = "--keyring";
      Args[i++] = pubringpath.c_str();

      Configuration::Item const *Opts;
      Opts = _config->Tree("Acquire::gpgv::Options");
      if (Opts != 0)
      {
         Opts = Opts->Child;
	 for (; Opts != 0; Opts = Opts->Next)
         {
            if (Opts->Value.empty() == true)
               continue;
            Args[i++] = Opts->Value.c_str();
	    if(i >= 395) { 
	       std::cerr << "E: Argument list from Acquire::gpgv::Options too long. Exiting." << std::endl;
	       exit(111);
	    }
         }
      }
      Args[i++] = file;
      Args[i++] = outfile;
      Args[i++] = NULL;

      if (_config->FindB("Debug::Acquire::gpgv", false))
      {
         std::cerr << "Preparing to exec: " << gpgvpath;
	 for(unsigned int j=0;Args[j] != NULL; j++)
	    std::cerr << " " << Args[j];
	 std::cerr << std::endl;
      }
      int nullfd = open("/dev/null", O_RDONLY);
      close(fd[0]);
      // Redirect output to /dev/null; we read from the status fd
      dup2(nullfd, STDOUT_FILENO); 
      dup2(nullfd, STDERR_FILENO); 
      // Redirect the pipe to the status fd (3)
      dup2(fd[1], 3);

      putenv("LANG=");
      putenv("LC_ALL=");
      putenv("LC_MESSAGES=");
      execvp(gpgvpath.c_str(), (char **)Args);
             
      exit(111);
   }
   close(fd[1]);

   pipein = fdopen(fd[0], "r"); 
   
   // Loop over the output of gpgv, and check the signatures.
   size_t buffersize = 64;
   char *buffer = (char *) malloc(buffersize);
   size_t bufferoff = 0;
   while (1)
   {
      int c;

      // Read a line.  Sigh.
      while ((c = getc(pipein)) != EOF && c != '\n')
      {
         if (bufferoff == buffersize)
            buffer = (char *) realloc(buffer, buffersize *= 2);
         *(buffer+bufferoff) = c;
         bufferoff++;
      }
      if (bufferoff == 0 && c == EOF)
         break;
      *(buffer+bufferoff) = '\0';
      bufferoff = 0;
      if (_config->FindB("Debug::Acquire::gpgv", false))
         std::cerr << "Read: " << buffer << std::endl;

      // Push the data into three separate vectors, which
      // we later concatenate.  They're kept separate so
      // if we improve the apt method communication stuff later
      // it will be better.
      if (strncmp(buffer, GNUPGBADSIG, sizeof(GNUPGBADSIG)-1) == 0)
      {
         if (_config->FindB("Debug::Acquire::gpgv", false))
            std::cerr << "Got BADSIG! " << std::endl;
         BadSigners.push_back(string(buffer+sizeof(GNUPGPREFIX)));
      }
      
      if (strncmp(buffer, GNUPGNOPUBKEY, sizeof(GNUPGNOPUBKEY)-1) == 0)
      {
         if (_config->FindB("Debug::Acquire::gpgv", false))
            std::cerr << "Got NO_PUBKEY " << std::endl;
         NoPubKeySigners.push_back(string(buffer+sizeof(GNUPGPREFIX)));
      }

      if (strncmp(buffer, GNUPGVALIDSIG, sizeof(GNUPGVALIDSIG)-1) == 0)
      {
         char *sig = buffer + sizeof(GNUPGPREFIX);
         char *p = sig + sizeof("VALIDSIG");
         while (*p && isxdigit(*p)) 
            p++;
         *p = 0;
         if (_config->FindB("Debug::Acquire::gpgv", false))
            std::cerr << "Got VALIDSIG, key ID:" << sig << std::endl;
         GoodSigners.push_back(string(sig));
      }
   }
   fclose(pipein);

   waitpid(pid, &status, 0);
   if (_config->FindB("Debug::Acquire::gpgv", false))
   {
      std::cerr <<"gpgv exited\n";
   }
   
   if (WEXITSTATUS(status) == 0)
   {
      if (GoodSigners.empty())
         return "Internal error: Good signature, but could not determine key fingerprint?!";
      return NULL;
   }
   else if (WEXITSTATUS(status) == 1)
   {
      return "At least one invalid signature was encountered.";
   }
   else if (WEXITSTATUS(status) == 111)
   {
      return (string("Could not execute ") + gpgvpath +
	      string(" to verify signature (is gnupg installed?)")).c_str();
   }
   else
   {
      return "Unknown error executing gpgv";
   }
}

bool GPGVMethod::Fetch(FetchItem *Itm)
{
   URI Get = Itm->Uri;
   string Path = Get.Host + Get.Path; // To account for relative paths
   string keyID;
   vector<string> GoodSigners;
   vector<string> BadSigners;
   vector<string> NoPubKeySigners;
   
   FetchResult Res;
   Res.Filename = Itm->DestFile;
   URIStart(Res);

   // Run gpgv on file, extract contents and get the key ID of the signer
   const char *msg = VerifyGetSigners(Path.c_str(), Itm->DestFile.c_str(),
				      GoodSigners, BadSigners, NoPubKeySigners);
   if (GoodSigners.empty() || !BadSigners.empty() || !NoPubKeySigners.empty())
   {
      string errmsg;
      // In this case, something bad probably happened, so we just go
      // with what the other method gave us for an error message.
      if (BadSigners.empty() && NoPubKeySigners.empty())
         errmsg = msg;
      else
      {
         if (!BadSigners.empty())
         {
            errmsg += "The following signatures were invalid:\n";
            for (vector<string>::iterator I = BadSigners.begin();
		 I != BadSigners.end(); I++)
               errmsg += (*I + "\n");
         }
         if (!NoPubKeySigners.empty())
         {
             errmsg += "The following signatures couldn't be verified because the public key is not available:\n";
            for (vector<string>::iterator I = NoPubKeySigners.begin();
		 I != NoPubKeySigners.end(); I++)
               errmsg += (*I + "\n");
         }
      }
      return _error->Error(errmsg.c_str());
   }
      
   // Transfer the modification times
   struct stat Buf;
   if (stat(Path.c_str(),&Buf) != 0)
      return _error->Errno("stat","Failed to stat %s", Path.c_str());

   struct utimbuf TimeBuf;
   TimeBuf.actime = Buf.st_atime;
   TimeBuf.modtime = Buf.st_mtime;
   if (utime(Itm->DestFile.c_str(),&TimeBuf) != 0)
      return _error->Errno("utime","Failed to set modification time");

   if (stat(Itm->DestFile.c_str(),&Buf) != 0)
      return _error->Errno("stat","Failed to stat");
   
   // Return a Done response
   Res.LastModified = Buf.st_mtime;
   Res.Size = Buf.st_size;
   // Just pass the raw output up, because passing it as a real data
   // structure is too difficult with the method stuff.  We keep it
   // as three separate vectors for future extensibility.
   Res.GPGVOutput = GoodSigners;
   Res.GPGVOutput.insert(Res.GPGVOutput.end(),BadSigners.begin(),BadSigners.end());
   Res.GPGVOutput.insert(Res.GPGVOutput.end(),NoPubKeySigners.begin(),NoPubKeySigners.end());
   URIDone(Res);

   if (_config->FindB("Debug::Acquire::gpgv", false))
   {
      std::cerr <<"gpgv suceeded\n";
   }

   return true;
}


int main()
{
   GPGVMethod Mth;

   return Mth.Run();
}
