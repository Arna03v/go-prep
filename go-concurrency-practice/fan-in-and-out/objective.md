### Exercise 2: Fan-In / Fan-Out

**Objective:** Distribute work across multiple processors and safely multiplex their outputs into a single stream.

* **Fan-Out:** Write a `generate` function that yields numbers 1-100 into a channel. Pass this channel to **two separate** `processor` goroutines. One processor multiplies the number by 2; the other squares it.
* **Fan-In:** Both processors must write their results to a *single* `merged` channel.
* **Constraint:** You cannot cause a panic by trying to close the `merged` channel twice. You must safely determine when both processors are completely done before closing the `merged` channel.