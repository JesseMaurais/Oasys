#include "bug.hpp"
#include "arg.hpp"
#include "str.hpp"
#include "type.hpp"
#include "esc.hpp"
#include "sym.hpp"
#include "dev.hpp"
#include "sig.hpp"
#include "err.hpp"
#include <fstream>
#include <future>
#include <vector>
#include <set>
#include <map>

namespace
{	
	void runner(fmt::string::view name, fmt::string::buf::ptr buf)
	{
		auto& out = std::cerr;
		auto back = out.rdbuf();
		try
		{
			out.rdbuf(buf);
			auto call = sys::sym<void()>(name);
			if (nullptr == call)
			{
				out << name << " is missing";
			}
			else
			{
				call();
			}
		}
		catch (std::exception const& error)
		{
			out << error.what() << fmt::eol;
		}
		catch (...)
		{
			out << "Unknown" << fmt::eol;
		}
		out.rdbuf(back);
	}
}

int main(int argc, char** argv)
{
	#ifndef _TOOLS
	# define _TOOLS "Tools.ini"
	#endif

	// Default options file
	fmt::string::view const tools = _TOOLS;

	// Command line words
	struct
	{
		env::opt::word const
			tests = fmt::str::put("TESTS"),
			color = fmt::str::put("color"),
			async = fmt::str::put("async"),
			tools = fmt::str::put("tools"),
			print = fmt::str::put("print"),
			help  = fmt::str::put("help");

	} key;

	// Command line details
	std::vector<env::opt::command> cmd
	{
		{ 0, "h", fmt::str::get(key.help), "Print command line usage then quit" },
		{ 0, "p", fmt::str::get(key.print), "Print all source tests then quit" },
		{ 0, "c", fmt::str::get(key.color), "Print using color codes" },
		{ 0, "a", fmt::str::get(key.async), "Run tests asynchronously" },
		{ 1, "t", fmt::str::get(key.tools), "Use instead of " _TOOLS },
	};

	// Command line parsing
	auto tests = env::opt::put(argc, argv, cmd);

	// Command line options
	auto const color = env::opt::get(key.color, true);
	auto const async = env::opt::get(key.async, false);
	auto const config = env::opt::get(key.tools, tools);
	auto const clean = std::empty(env::opt::arguments);

	// Initialize from tools
	if (not std::empty(config))
	{
		std::ifstream in { config.data() };
		if (not (in >> env::opt::get))
		{
			std::cerr << "Failed to read " << config;
		}
	}

	// Default test options
	if (std::empty(tests))
	{
		const auto list = env::opt::get(key.tests);
		for (const auto test : fmt::split(list))
		{
			tests.emplace_back(test);
		}
	}

	// Map test names to error buffers' string stream
	std::map<fmt::string::view, fmt::string::stream> context;
	fmt::string::view const program = env::opt::program;
	fmt::string const prefix = "test_";

	if (std::empty(tests))
	{
		env::dev::dump dump; // output cache

		// Parse this programs symbol table
		for (auto const& line : dump.dyn(program))
		{
			// Separate lines by white space
			for (auto const name : fmt::split(line))
			{
				// Match those with prefix
				if (name.starts_with(prefix))
				{
					// Symbol must exist
					auto const call = sys::sym<void()>(name);
					if (nullptr != call)
					{
						verify(context[name].str().empty());
					}
				}
			}
		}
	}
	else // copy
	{
		for (fmt::string const name : tests)
		{
			auto const call = sys::sym<void()>(prefix + name);
			if (nullptr == call)
			{
				std::cerr << "Cannot find " << name << " in " << program;
			}
			else
			{
				verify(context[name].str().empty());
			}
		}
	}

	// Print the unit tests and quit
	if (env::opt::get(key.print, false))
	{
		for (auto const& [name, error] : context)
		{
			std::cerr << name << fmt::eol;
		}
		return EXIT_SUCCESS;
	}

	bool const missing = clean and std::empty(context);
	// Print the help menu and quit if missing
	if (env::opt::get(key.help, missing))
	{
		if (missing)
		{
			if (color) std::cerr << fmt::fg_yellow;

			std::cerr << "No tests were found" << fmt::eol;

			if (color) std::cerr << fmt::fg_off;
		}

		std::cerr 
			<< "Unit tests are found in order:"
			<< fmt::eol << fmt::tab
			<< "1. Free command line arguments"
			<< fmt::eol << fmt::tab
			<< "2. The TESTS environment variable"
			<< fmt::eol << fmt::tab
			<< "3. The TESTS variable in " _TOOLS
			<< fmt::eol << fmt::tab
			<< "4. The dump symbols for " << prefix << "*"
			<< fmt::eol
			<< "Commands for unit test runner:"
			<< fmt::eol;

		for (auto const & item : cmd)
		{
			std::cerr
				<< fmt::tab
				<< env::opt::dash << item.dash
				<< fmt::tab
				<< env::opt::dual << item.name
				<< fmt::tab
				<< item.text
				<< fmt::eol;
		}
		return EXIT_SUCCESS;
	}

	// Run all the selected unit tests 
	{
		std::vector<std::future<void>> threads;

		// Run tests either in serial or parallel
		for (auto const& [name, error] : context)
		{
			auto buf = error.rdbuf();
			if (async)
			{
				threads.emplace_back
				(
					std::async(std::launch::async, runner, name, buf)
				);
			}
			else
			{
				runner(name, buf);
			}
		}

		// Wait on test completion
		for (auto& job : threads)
		{
			job.wait();
		}
	}

	if (color)
	{
		std::cerr << fmt::fg_yellow;
	}

	std::size_t counter = 0;
	for (auto& [name, error] : context)
	{
		if (auto str = error.str(); not std::empty(str))
		{
			while (std::getline(error, str))
			{
				std::cerr << name << fmt::tab << str << fmt::eol;
				++ counter;
			}
		}
	}

	if (color)
	{
		std::cerr << (0 < counter ? fmt::fg_magenta : fmt::fg_cyan);
	}

	std::cerr << "There are " << counter << " errors" << fmt::eol;

	if (color)
	{
		std::cerr << fmt::reset;
	}

	return 0 < counter ? EXIT_FAILURE : EXIT_SUCCESS;
}

