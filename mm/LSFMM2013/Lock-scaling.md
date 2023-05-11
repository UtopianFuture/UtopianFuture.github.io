## Lock scaling

kernel developers have worked to **improve scalability by adding increasingly fine-grained locking** to the kernel. Locks with small coverage help to avoid contention, but, when there *is* contention, **the resulting communication（不理解是什么）** overhead wipes out the advantages that fine-grained locks are supposed to provide.

Processing single objects within a lock is not a scalable approach. Processing multiple objects under a single lock will amortize the cost of that lock over much more work, increasing the overall scalability of the system.

没有后续讨论。

