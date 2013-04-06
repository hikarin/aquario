= What's Aquario

Aquario is a GC-oriented Lisp interpreter. 

== Features of Aquario
* Lisp-1
* Multiple and Selectable Garbage Collectors
* Support for Implementing GC Algorithms(ex. root scan, object traverse, and
  some basic algorithms such as Cheney's Copying collector)

== How to run Aquario

  You can build Aquario

    $ make

  Then put the following command to run Aquario with Cheney's Copying collector:

    $ ./aquario -GC copying

  Or to run Aquario with Mark and Compact collector, put the following command:

    $ ./aquario -GC mark_compact
