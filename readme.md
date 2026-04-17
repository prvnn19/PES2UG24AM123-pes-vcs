# PES-VCS Implementation Report
**Author:** Prajwalindra H  
**SRN:** PES2UG24AM117  
**Repository:** [PES2UG24AM117-pes-vcs](https://github.com/proindra/PES2UG24AM117-pes-vcs)

---

## 📸 Implementation Screenshots

### Phase 1: Object Storage Foundation
* **Screenshot 1A (Test Objects Pass):** ![Screenshot 1A](<Screenshots/SCREENSHOT 1A.png>)  
  *(Placeholder: Replace with your actual screenshot of `./test_objects` passing)*

* **Screenshot 1B (Sharded Directory):** ![Screenshot 1B](<Screenshots/SCREENSHOT 1B.png>)  
  *(Placeholder: Replace with your actual screenshot of `find .pes/objects -type f`)*

### Phase 2: Tree Objects
* **Screenshot 2A (Test Tree Pass):** ![Screenshot 2A](<Screenshots/SCREENSHOT 2A.png>)  
  *(Placeholder: Replace with your actual screenshot of `./test_tree` passing)*

* **Screenshot 2B (Raw Tree Hex):** ![Screenshot 2B](<Screenshots/SCREENSHOT 2B.png>)  
  *(Placeholder: Replace with your actual screenshot of `xxd` on a tree object)*

### Phase 3: The Index (Staging Area)
* **Screenshot 3A (Status Output):** ![Screenshot 3A](<Screenshots/SCREENSHOT 3A.png>)  
  *(Placeholder: Replace with your actual screenshot of `pes status` showing staged files)*

* **Screenshot 3B (Raw Index File):** ![Screenshot 3B](<Screenshots/SCREENSHOT 3B.png>)  
  *(Placeholder: Replace with your actual screenshot of `cat .pes/index`)*

### Phase 4: Commits and History
* **Screenshot 4A (Commit Log):** ![Screenshot 4A](<Screenshots/SCREENSHOT 4A.png>)  
  *(Placeholder: Replace with your actual screenshot of `pes log`)*

* **Screenshot 4B (Object Store Growth):** ![Screenshot 4B](<Screenshots/SCREENSHOT 4B.png>)  
  *(Placeholder: Replace with your actual screenshot showing objects after commits)*

* **Screenshot 4C (Reference Chain):** ![Screenshot 4C](<Screenshots/SCREENSHOT 4C.png>)  
  *(Placeholder: Replace with your actual screenshot of `HEAD` and `main` refs)*

### Phase 5: Final Integration
* **Final Integration Screenshot:** ![Final Part 1](<Screenshots/FINAL SCREENSHOT part 1.png>) ![Final Part 2](<Screenshots/FINAL SCREENSHOT part 2.png>)  
  *(Placeholder: Replace with your actual screenshot of `make test-integration` passing)*

---

## 📝 Analysis Questions

### Q5.1: Branching and Checkout Implementation
**How to implement checkout — what files need to change in `.pes/`, and what must happen to the working directory? What makes this operation complex?**

To implement `pes checkout <branch>`, the system must first read the branch reference file (e.g., `.pes/refs/heads/<branch>`) to find the target commit hash. From that commit, it reads the root tree hash and recursively walks the tree to reconstruct the directory and file structure in the physical working directory. 

Inside `.pes/`, the `HEAD` file must be updated to point to the newly checked-out branch (`ref: refs/heads/<branch>`), and the `.pes/index` file must be rewritten to match the exact state of the newly loaded tree.

The complexity lies in state management. The operation must safely delete files that exist in the current branch but not the target branch, while strictly avoiding the deletion or overwriting of any uncommitted work (dirty files) the user currently has in their working directory.

### Q5.2: Detecting a Dirty Working Directory
**Describe how you would detect this "dirty working directory" conflict using only the index and the object store.**

A "dirty" working directory occurs when the working files do not match the currently staged index or the latest commit. To detect a conflict during checkout, I would:
1. Scan the working directory and calculate the hashes of all tracked files (or use `stat` metadata like `mtime` and `size` for a fast comparison against the index, as implemented in `index_status`).
2. Compare the index against the target branch's root tree. 
3. If a file has uncommitted modifications (dirty) *and* that specific file differs between the current HEAD tree and the target branch tree, a conflict is triggered. The checkout must refuse to run to prevent silently overwriting the user's unsaved local changes.

### Q5.3: Detached HEAD State
**What happens if you make commits in this state? How could a user recover those commits?**

In a Detached HEAD state, the `.pes/HEAD` file contains a raw commit hash instead of a branch reference (like `ref: refs/heads/main`). If you make commits in this state, the new commit objects are successfully created in the object store, and HEAD moves to point to them. However, no branch reference file is updated.

If the user then checks out a different branch (e.g., `pes checkout main`), HEAD is rewritten. The commits made during the detached state become "unreachable" or "dangling" because no branch pointer references them. To recover them, the user must remember the hash of the last detached commit and create a new branch pointing to it. In a real Git system, the user could use `git reflog`, which keeps a local history of where HEAD has pointed, to find the lost hash and recover the branch.

### Q6.1: Garbage Collection and Space Reclamation
**Describe an algorithm to find and delete these objects. What data structure would you use to track "reachable" hashes efficiently?**

A standard Mark-and-Sweep algorithm is required for garbage collection (GC). 
1. **Mark Phase:** Iterate through all branch files in `.pes/refs/heads/` and the `HEAD` file. For each commit found, "mark" it as reachable. Then, traverse its parent commits recursively, marking all of them. For every marked commit, load its root tree, and recursively mark all sub-trees and blobs it references.
2. **Sweep Phase:** Iterate through all files in the `.pes/objects/` directory. If a file's hash is not in the "marked" collection, it is unreachable and can be safely deleted.

The most efficient data structure for tracking reachable hashes is a **Hash Set**. It provides $O(1)$ lookups and prevents processing the same tree or blob multiple times. Even with 100,000 commits, deduplication ensures the traversal remains efficient by halting recursion when an already-marked object is encountered.

### Q6.2: Garbage Collection Race Conditions
**Why is it dangerous to run garbage collection concurrently with a commit operation? Describe a race condition... How does Git's real GC avoid this?**

Running GC concurrently with a commit operation introduces a severe race condition. Imagine a user runs `pes add file.txt`. A new blob object is written to `.pes/objects/`. Immediately after, a concurrent GC process begins its "Mark" phase. Because the user hasn't run `pes commit` yet, this new blob is not referenced by any commit or tree. The GC process determines the blob is unreachable. 

As the user initiates the `pes commit` operation, the GC process enters its "Sweep" phase and deletes the newly created blob. The commit completes, creating a tree that points to a deleted blob, resulting in a corrupted repository.

Real Git avoids this by enforcing a "grace period" (typically 2 weeks). During `git gc`, any unreachable object that has a modification timestamp newer than the grace period is strictly preserved. This ensures that objects currently being staged or generated by concurrent commands are never deleted