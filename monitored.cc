#include <array>
#include <chrono>
#include <functional>
#include <iostream>
#include <libgen.h>
#include <optional>
#include <sstream>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

bool debug_enabled = false;
#define debug debug_enabled&& std::cerr

template <typename T>
void execvp_array(T* args)
{
	debug << "execvp:";
	for (int i = 0; args[i] != nullptr; ++i)
	{
		debug << " '" << args[i] << "'";
	}
	debug << std::endl;
	execvp(args[0], const_cast<char**>(args));
	exit(EXIT_FAILURE);
}

void exit_with(int const status)
{
	// only the 8 least significant bits are returned to the waiting process
	// https://pubs.opengroup.org/onlinepubs/9699919799/functions/exit.html
	auto const linux_status = status & 0xFF;
	exit(status == linux_status ? status : EXIT_FAILURE);
}

void execvp_vector(std::vector<std::string_view>&& args)
{
	std::vector<char const*> cargs;
	for (auto arg : args)
	{
		// No need to care about memory, as exec will reclaim it
		auto s = new std::string(arg);
		cargs.push_back(s->c_str());
	}
	execvp_array(cargs.data());
}

pid_t fork_with(std::function<void()> child, std::function<void(pid_t)> parent)
{
	auto const pid = fork();
	if (pid < 0)
	{
		std::cerr << "fork failed" << std::endl;
		exit_with(EXIT_FAILURE);
	}
	pid == 0 ? child() : parent(pid);
	return pid;
}

int wait_for(
    pid_t pid,
    std::function<void(int)> callback =
        [](int const status)
    {
	    if (status != EXIT_SUCCESS) exit_with(status);
    })
{
	int status;
	waitpid(pid, &status, 0);
	callback(status);
	return status;
}

std::array<int, 2> make_pipe()
{
	std::array<int, 2> fd;
	if (pipe(fd.data()) == -1)
	{
		std::cerr << "pipe failed" << std::endl;
		exit(EXIT_FAILURE);
	}
	return fd;
}

void notify(int const status, char* argv[])
{
	bool const success = status == EXIT_SUCCESS;
	debug << "sending " << (success ? "success" : "failure") << " notification"
	      << std::endl;
	std::string_view const icon(NOTIFY_ICON);
	std::string_view const urgency(success ? "low" : "critical");
	std::stringstream title;
	title << "Nix command " << (success ? "succeeded" : "failed");
	std::stringstream message;
	message << "<span font='monospace'>";
	for (int i = 0; argv[i] != nullptr; ++i)
	{
		message << (i > 0 ? " " : "");
		bool const has_space =
		    std::string_view(argv[i]).find(' ') != std::string_view::npos;
		message << (has_space ? "'" : "") << argv[i] << (has_space ? "'" : "");
	}
	message << "</span>";

	fork_with(
	    [&]()
	    {
		    execvp_vector(
		        {"notify-send",
		         "--icon",
		         icon,
		         "--urgency",
		         urgency,
		         title.view(),
		         message.view()});
	    },
	    [&](auto const pid)
	    {
		    wait_for(pid);
	    });
}

int main(int argc, char* argv[])
{
	for (int i = 1;; ++i)
	{
		if (debug_enabled) argv[i-1] = argv[i];
		if (argv[i] == nullptr) break;
		std::string_view const arg(argv[i]);
		if (arg == "--" || arg == "--command") break;
		if (arg == "--debug")
		{
			debug_enabled = true;
			debug << "debug output enabled" << std::endl;
		}
	}
	std::string const path(std::string(PATH) + ":" + getenv("PATH"));
	setenv("PATH", path.c_str(), 1);
	// We should use the name of the executable from the PATH, rather than the
	// full path we were called with.
	argv[0] = basename(argv[0]);
#ifdef NOTIFY
	fork_with(
	    [&]() { /* run the rest of main as a child */ },
	    [&](auto const pid)
	    {
		    debug << "notify timer started" << std::endl;
		    auto const start = std::chrono::steady_clock::now();
		    auto const status =
		        wait_for(pid, [](auto) { /* do nothing on error */ });
		    auto const elapsed = std::chrono::steady_clock::now() - start;
		    debug << "notify timer stopped after "
		          << std::chrono::duration<double>(elapsed).count() << " s "
		          << "with status " << status << std::endl;
		    if (elapsed > std::chrono::seconds(2)) notify(status, argv);
		    exit_with(status);
	    });
#endif
	if (!isatty(fileno(stderr)) || argc < 2)
	{
		execvp_array(argv);
	}
	debug << "argv:";
	for (int i = 0; argv[i] != nullptr; ++i)
	{
		debug << " " << argv[i];
	}
	debug << std::endl;
	std::string_view const command(argv[0]);
	std::string_view const verb(argv[1]);
	// Trivial cases: nom supports builds and shells
	// We also want to print nom's version, not Nix' version.
	if (command == "nix-build" || verb == "build" || command == "nix-shell" ||
	    verb == "shell" || verb == "develop" || verb == "--version")
	{
		argv[0][1] = 'o';
		argv[0][2] = 'm';
		execvp_array(argv);
	}
	// `nix run` first builds the derivation. We can `nom build` it.
	// But we don't want nom to wrap around the run, so we `nix run` it.
	if (verb == "run")
	{
		fork_with(
		    [&]()
		    {
			    std::vector<std::string_view> nom_args{
			        "nom", "build", "--no-link"};
			    for (int i = 2; i < argc && argv[i] != nullptr; ++i)
			    {
				    std::string_view const arg(argv[i]);
				    if (arg == "--" || arg == "--command") break;
				    nom_args.push_back(argv[i]);
			    }
			    execvp_vector(std::move(nom_args));
		    },
		    [&](auto nom_pid)
		    {
			    wait_for(nom_pid);
			    execvp_array(argv);
		    });
	}
	// These verbs should not be wrapped by nom, as they don't do any building.
	if (verb == "repl" || verb == "flake" || verb == "log" || verb == "eval" ||
	    verb == "--help")
	{
		execvp_array(argv);
	}
	// For every other command-verb combo, run `<command> <args> |& nom`
	auto const nix_stderr = make_pipe();
	fork_with(
	    [&]()
	    {
		    close(nix_stderr[0]);
		    dup2(nix_stderr[1], STDERR_FILENO);
		    std::vector<std::string_view> nix_args{
		        argv[0], "--log-format", "internal-json"};
		    for (int i = 1; i < argc && argv[i] != nullptr; ++i)
		    {
			    nix_args.push_back(argv[i]);
		    }
		    execvp_vector(std::move(nix_args));
	    },
	    [&](auto const nix_pid)
	    {
		    fork_with(
		        [&]()
		        {
			        close(nix_stderr[1]);
			        dup2(nix_stderr[0], STDIN_FILENO);
			        execvp_vector({"nom", "--json"});
		        },
		        [&](auto const nom_pid)
		        {
			        close(nix_stderr[0]);
			        close(nix_stderr[1]);
			        wait_for(nix_pid);
			        wait_for(nom_pid);
			        exit(EXIT_SUCCESS);
		        });
	    });
}
