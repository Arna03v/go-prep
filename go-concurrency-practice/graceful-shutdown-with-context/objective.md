### Exercise 3: Graceful Shutdown with Context

**Objective:** Catch an OS interrupt signal and safely drain ongoing work before exiting.

* **Setup:** Create a continuous loop inside a goroutine that prints "Working..." every 500ms.
* **The Trap:** Use `signal.NotifyContext(context.Background(), os.Interrupt)` to listen for a `Ctrl+C` (SIGINT) event.
* **Constraint 1:** The main function must block and wait for the signal.
* **Constraint 2:** When `Ctrl+C` is pressed, the worker loop must detect that the context is canceled (using `ctx.Done()`), print "Cleaning up...", wait 1 second to simulate cleanup, and safely exit.
* **Constraint 3:** The program must not forcefully terminate immediately when `Ctrl+C` is pressed; it must wait for the cleanup to finish.