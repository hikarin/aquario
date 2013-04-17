= What's Aquario

Aquario is a GC-oriented Lisp interpreter. 

== Features of Aquario
* Lisp-1
* Supports for Implementing GC Algorithms (ex. root scan, object traverse and
  write barriers)
* Multiple and Selectable Garbage Collectors (so far, Cheney's copying collector,
  simple Mark-Compact collector and Reference Counting with Zero Count Table)

== How to run Aquario

  Aquario can be built with the following command:

    $ make

  Then put the following command to run Aquario with Cheney's Copying collector:

    $ ./aquario -GC copying

  Or to run Aquario with simple Mark-Compact collector, put the following command:

    $ ./aquario -GC mark_compact

== Future work

* Bytecode Interpreter
* Read Barrier
* More Garbage Collectors such as Yuasa's Snapshot collector