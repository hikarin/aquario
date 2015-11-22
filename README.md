# What's Aquario [![Build Status](https://travis-ci.org/hikarin/aquario.png?branch=master)](https://travis-ci.org/hikarin/aquario)

Aquario is a GC-oriented Lisp interpreter. 

## Features of Aquario
* Lisp-1
* Supports for Implementing GC Algorithms
  (ex. root scan, object traverse, write barriers, etc.)
* Multiple and Selectable Garbage Collectors:
   - Cheney's Copying collector
   - Mark-Compact collector
   - Reference Counting
   - Generational Collector

## Target persons

  Aquario is for someone who
  * wants to learn how Garbage Collector is implemented
  * wants to implement Garbage Collector
  * loves Garbage Collection

## How to run Aquario

  Aquario can be built with the following command:

    $ make

  Then put the following command to run Aquario with Cheney's Copying collector:

    $ ./aquario -GC copy

  So far, Aquario receives "copy", "mc", "ref" and "gen" with -GC option.

## Future work

* More supports for GC such as Read Barrier
* More Garbage Collectors such as Yuasa's Snapshot collector
* Visualization
* Profiler
