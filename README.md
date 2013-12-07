= What's Aquario

Aquario is a GC-oriented Lisp interpreter. 

== Features of Aquario
* Lisp-1
* Supports for Implementing GC Algorithms
  (ex. root scan, object traverse and write barriers)
* Multiple and Selectable Garbage Collectors (so far, Cheney's copying collector,
  simple Mark-Compact collector and Reference Counting with Zero Count Table)

== Target persons

  Aquario is for someone who
  * wants to learn how Garbage Collector is implemented
  * wants to implement Garbage Collector
  * loves Garbage Collection

== How to run Aquario

  Aquario can be built with the following command:

    $ make

  Then put the following command to run Aquario with Cheney's Copying collector:

    $ ./aquario -GC copying

  So far, Aquario receives "copying", "mark_compact" and "reference_count" with -GC option.

== Future work

* More supports for GC such as Read Barrier
* More Garbage Collectors such as Yuasa's Snapshot collector
* Visualization
* Profiler
