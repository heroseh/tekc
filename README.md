# Tek

Tek is a WIP systems programming language to replace C. The main focus is to give the programmer full control of what the computer does.
Allowing the programmer to build their own solid abstractions. It is primarily inspired by C, and many of the new systems programming languages that have been gaining traction lately.

### Why am I making my own language? <br/>
I love pushing myself by learning new things and I have ideas.

### Want to see live development? <br/>
Catch the development stream on [Twitch](https://twitch.tv/heroseh) every Thursday and Friday @ 1PM GMT

# Aims
- A simple language like C but with some modifications & extra features
	- Sounds very familiar I know ;)
- No garbage collection, No Virtual Machine and No OOP features
- Build a full MVP compiler that converts code to an executable
	- At first the language will only have procedures, statements and expressions
	- We will always keep a working build and expand from there
- Compile straight to naive machine code first and then leverage LLVM for optimized builds
- Multithreaded and as lock free as possible.
- Fast compile times
	- Try to have a Data Oriented Design


# What do you have working right now?
- Multithreaded worker and job system
- A lock free string table
	- allows us to compare strings throughout the system by comparing a single integer
- OS Virtual Memory Mapping abstraction, that reserves large ranges of address space to avoid reallocating.
- A Lexer that creates a list of tokens directly from the code

# What is next?
1. Create a syntax parser to create an syntax tree from the tokens.
1. Create a semantic parser to create a semantic tree from the syntax tree.
1. Create a simple assembly abstraction (something like LLVM IR but not SSA).
1. Generate X86-64 from that assembly abstraction.
1. Generate an ELF executable that has the generated X86-64 instruction code embedded in it.

# Want to see the Compiler in action?

Compile the build script that is written in C
```
./build_build_script.sh
```

Run the build script to compile the Compiler
```
./build_script --debug
```

Run the Compiler on a test file
```
./build/tekc tests/run_pass/expr.tek
```

Now open up **tests/run_pass/expr.tek** and **/tmp/tek_tokens** side by side and see the file broken up into tokens.

