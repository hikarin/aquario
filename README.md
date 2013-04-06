= What's Aquario

Aquario is a GC-oriented Lisp interpreter. 

== Features of Aquario
* Lisp-1
* Multiple and Selectable Garbage Collectors
* Support for Implementing GC Algorithms(ex. root scan, object traverse, and
  some simple Garbage Collector such as Cheney's Copying collector)

== How to run Aquario

  You can build Aquario with the following command:

    $ make

  Then put the following command to run Aquario with Cheney's Copying collector:

    $ ./aquario -GC copying

  Or to run Aquario with Mark and Compact collector, put the following command:

    $ ./aquario -GC mark_compact

== Future work

* Bytecode Interpreter
* More Garbage Collectors such as Yuasa's Snapshot collector