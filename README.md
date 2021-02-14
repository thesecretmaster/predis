# Predis

This is an in-memory datastore that mirrors the functionality and interface of [redis](https://github.com/redis/redis). However, the similarities end there – internally we take advantage of lock free data structures and parallelism to achieve significantly higher throughput on a single instance than redis would be capable of on multicore machines, and comparable performance on single core machines.

## Installation / Environment Configuration

```
> git clone https://github.com/thesecretmaster/predis
> make
> bin/server
```

The build on master may show some warnings but should always be functional. This will also build `bin/server_dbg` which compiles without optimizations for easier debugging with `gdb`.

Predis implements the `RESP` protocol which is also used by redis, so you can interact with predis using the `redis-cli` interface provided by redis.

## Status

Currently this project is fully functional on `get` and `set` commands and can be benchmarked with `redis-benchmark`. The engine and type/command support could use a lot of work and those are the main focuses at the moment.

## Roadmap

There are two main directions that can be worked on in this project:

1. Adding new types and commands to work towards feature parity with redis
2. Engine improvements and performance optimizations

### Engine Improvements

The engine lives in `network_parser.c` and uses many of the resources in `lib/`. General improvements in this area will come from things like reducing system calls, restructuring code for better cache locality, redesigning interfaces and algorithms.

Additionally, this is where the interface that commands and types use lives (in addition to parts in `commands.c`), and many optimizations in that area are certainly possible.

If this sounds interesting to you, check out the issues tagged `engine` or`command-interface` in the issue tracker!

### Command / Type Improvements

Commands and types live in `commands/` and `types/` respectively. Their interfaces aren't formally documented (it's on the to do list!) but should be fairly intuitive from the headers in the `public/` directory and from looking at the `string` or `hash` types which have some basic work done.

 To add new commands and types, you simply add the files to the correct directories. To load a command or type by default, you can add a line to the `main` function in `network_parser.c` of the form `load_structures(&ctx, NULL, &((char*){"<types or commands>/my_type_or_command.so"}), NULL, 1);`, or you can call `load <filename>` at runtime to load an arbitrary module into predis.

Generally, anything that's in redis is fair game to add, but if there's something else you're interested in feel free to try it out! Just remember that your type and command has to be thread safe and ideally lock free.

