# What's Aquario [![Build Status](https://travis-ci.org/hikarin/aquario.png?branch=master)](https://travis-ci.org/hikarin/aquario)

Aquario is a GC-oriented Lisp interpreter. 

## Features of Aquario
* Lisp-1
* Supports for Implementing GC Algorithms
  (ex. root scan, object traverse, write barriers, etc.)
* Multiple and Selectable Garbage Collectors:
   - Mark-Sweep collector
   - Cheney's Copying collector
   - Mark-Compact collector
   - Reference Counting
   - Generational Collector

## Target persons

  Aquario is for someone who
  * wants to learn how Garbage Collector is implemented
  * wants to implement Garbage Collector
  * loves Garbage Collection

## How to run

### Prerequisite
 * CMake

### For macOS or Linux
Make a directory to build the binary, execute `cmake` and `make`, then execute the generated binary `aquario`
```
$ mkdir build
$ cd build
$ cmake ..
$ make
$ ./aquario
```

### For Windows

1. Launch CMake GUI
2. Enter the source code path and the build path
3. Press "Configure", then press "Generate"
![cmake-gui](https://user-images.githubusercontent.com/188830/103162501-65f7db00-47bf-11eb-87bc-66ee9c02f47a.PNG)
4. Open aquario.sln with Visual Studio
5. Press "Build Solution"
6. Press "Start Debugging" or F5 to launch Aquario

## How to test
To ensure that all GCs are working properly, you can do:

### For macOS or Linux

`$ make tests`

### For Windows

Switch to Folderview and click "Run All" in Text Explorer view.
![visual-studio](https://user-images.githubusercontent.com/188830/103162525-b2dbb180-47bf-11eb-9c98-bb1341a39424.PNG)

## Future work

* More supports for GC such as Read Barrier
* More Garbage Collectors such as Yuasa's Snapshot collector
* Visualization
* Profiler

## License

  - NYSL
    http://www.kmonos.net/nysl/index.en.html
