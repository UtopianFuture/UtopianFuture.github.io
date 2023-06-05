## Shrinkers

Shrinkers are callbacks that can be registered with the memory management subsystem, they are called when memory is tight in the hope that they will free up some resources. There are a number of problems with shrinkers, starting with the fact that they are a global operation while most memory problems are localized to a specific zone or NUMA node.

In addition, there is little agreement over what a call to a shrinker really means or how the called subsystem should respond.

The maintainer has a solution in the form of a reworked shrinker API. It starts with a generic, per-NUMA-node LRU list that can be adapted to whatever type of object a specific shrinker needs to manage. The shrinker interface itself is built into the list. The result is the wholesale replacement of a lot of custom list management and shrinker code. Shrinkers become a lot more consistent in their effects and the chances of implementers getting things wrong are much reduced.
