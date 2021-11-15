//
// subprocess C++ library - https://github.com/tsaarni/cpp-subprocess
//
// The MIT License (MIT)
//
// Copyright (c) 2015 Tero Saarni
//

#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <ext/stdio_filebuf.h>
#include <cstdio>
#include <system_error>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <signal.h>

namespace subprocess
{

    class popen
    {
        pid_t _pid = 0;

        std::array<int,2> in_pipe = {0,0};
        std::array<int,2> out_pipe = {0,0};
        std::array<int,2> err_pipe = {0,0};

        __gnu_cxx::stdio_filebuf<char>* in_filebuf = nullptr;
        __gnu_cxx::stdio_filebuf<char>* out_filebuf = nullptr;
        __gnu_cxx::stdio_filebuf<char>* err_filebuf = nullptr;

        std::ostream* in_stream = nullptr;
        std::istream* out_stream = nullptr;
        std::istream* err_stream = nullptr;

    public:
        popen() = default;
        popen(const popen & oth) = delete;
        popen(popen && oth)  :
            _pid(oth._pid),
            in_pipe(oth.in_pipe),
            out_pipe(oth.out_pipe),
            err_pipe(oth.err_pipe),
            in_filebuf(oth.in_filebuf),
            out_filebuf(oth.out_filebuf),
            err_filebuf(oth.err_filebuf),
            in_stream(oth.in_stream),
            out_stream(oth.out_stream),
            err_stream(oth.err_stream)
        {
            oth.in_filebuf = nullptr;
            oth.out_filebuf = nullptr;
            oth.err_filebuf = nullptr;
            oth.in_stream  = nullptr;
            oth.out_stream  = nullptr;
            oth.err_stream  = nullptr;
        }

        popen& operator=(const popen & oth) = delete;
        popen& operator=(popen && oth)
        {
            _pid = oth._pid;
            in_pipe = oth.in_pipe;
            out_pipe = oth.out_pipe;
            err_pipe = oth.err_pipe;
            
            in_filebuf = oth.in_filebuf;
            out_filebuf = oth.out_filebuf;
            err_filebuf = oth.err_filebuf;
            in_stream = oth.in_stream;
            out_stream = oth.out_stream;
            err_stream = oth.err_stream;

            oth.in_filebuf = nullptr;
            oth.out_filebuf = nullptr;
            oth.err_filebuf = nullptr;
            oth.in_stream  = nullptr;
            oth.out_stream  = nullptr;
            oth.err_stream  = nullptr;

            return *this;
        }

        popen(const std::string& cmd, std::vector<std::string> argv)        
        {
            if (pipe_arr(in_pipe)  == -1 ||
                    pipe_arr(out_pipe) == -1 ||
                    pipe_arr(err_pipe) == -1 )
            {
                throw std::system_error(errno, std::system_category());
            }

            run(cmd, argv);
        }

        popen(const std::string& cmd, std::vector<std::string> argv, std::ostream& pipe_stdout)
        {
            auto filebuf = dynamic_cast<__gnu_cxx::stdio_filebuf<char>*>(pipe_stdout.rdbuf());
            out_pipe[READ]  = -1;
            out_pipe[WRITE] = filebuf->fd();

            if (pipe_arr(in_pipe) == -1 ||
                    pipe_arr(err_pipe) == -1 )
            {
                throw std::system_error(errno, std::system_category());
            }

            run(cmd, argv);
        }

        ~popen()
        {
            delete in_filebuf;
            delete in_stream;
            if (out_filebuf != nullptr) delete out_filebuf;
            if (out_stream  != nullptr) delete out_stream;
            delete err_filebuf;
            delete err_stream;
        }

        std::ostream& stdin()  { return *in_stream;  };

        std::istream& stdout()
        {
            if (out_stream == nullptr) throw std::system_error(EBADF, std::system_category());
            return *out_stream;
        };

        std::istream& stderr() { return *err_stream; };

        int wait()
        {
            int status = 0;
            waitpid(_pid, &status, 0);
            return WEXITSTATUS(status);
        };

        void close()
        {
            in_filebuf->close();
        }

        void kill(int signum)
        {
            ::kill(_pid, signum);
        }

        void terminate()
        {
            kill(SIGTERM);
        }

        int pid() const
        {
            return _pid;
        }
    private:
        enum ends_of_pipe { READ = 0, WRITE = 1 };

        int pipe_arr(std::array<int,2>& arr) 
        {
            int _arr[2];
            int sts = pipe(_arr);
            arr = std::array<int,2>({_arr[0], _arr[1]});
            return sts;
        }

        struct raii_char_str
        {
            raii_char_str(std::string s) : buf(s.c_str(), s.c_str() + s.size() + 1) { };
            operator char*() const { return &buf[0]; };
            mutable std::vector<char> buf;
        };

        void run(const std::string& cmd, std::vector<std::string> argv)
        {
            argv.insert(argv.begin(), cmd);

            _pid = ::fork();

            if (_pid == 0) child(argv);

            ::close(in_pipe[READ]);
            ::close(out_pipe[WRITE]);
            ::close(err_pipe[WRITE]);

            in_filebuf = new __gnu_cxx::stdio_filebuf<char>(in_pipe[WRITE], std::ios_base::out, 1);
            in_stream  = new std::ostream(in_filebuf);

            if (out_pipe[READ] != -1)
            {
                out_filebuf = new __gnu_cxx::stdio_filebuf<char>(out_pipe[READ], std::ios_base::in, 1);
                out_stream  = new std::istream(out_filebuf);
            }

            err_filebuf = new __gnu_cxx::stdio_filebuf<char>(err_pipe[READ], std::ios_base::in, 1);
            err_stream  = new std::istream(err_filebuf);
        }

        void child(const std::vector<std::string>& argv)
        {
            if (dup2(in_pipe[READ], STDIN_FILENO)    == -1 ||
                    dup2(out_pipe[WRITE], STDOUT_FILENO) == -1 ||
                    dup2(err_pipe[WRITE], STDERR_FILENO) == -1 )
            {
                std::perror("subprocess: dup2() failed");
                return;
            }

            ::close(in_pipe[READ]);
            ::close(in_pipe[WRITE]);
            if (out_pipe[READ] != -1) ::close(out_pipe[READ]);
            ::close(out_pipe[WRITE]);
            ::close(err_pipe[READ]);
            ::close(err_pipe[WRITE]);

            std::vector<raii_char_str> real_args(argv.begin(), argv.end());
            std::vector<char*> cargs(real_args.begin(), real_args.end());
            cargs.push_back(nullptr);

            if (execvp(cargs[0], &cargs[0]) == -1)
            {
                std::perror("subprocess: execvp() failed");
                return;
            }
        }
    };
} // namespace: subprocess
