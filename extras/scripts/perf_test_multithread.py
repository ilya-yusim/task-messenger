#!/usr/bin/env python3
import threading
import time

running = True

def worker(i):
    # busy loop with periodic sleeps to create some activity
    while running:
        s = 0
        for j in range(10000):
            s += j * j
        time.sleep(0.01)

if __name__ == '__main__':
    threads = []
    for i in range(4):
        t = threading.Thread(target=worker, args=(i,), name=f"worker-{i}")
        t.start()
        threads.append(t)
    try:
        # run for 30s unless killed
        time.sleep(30)
    except KeyboardInterrupt:
        pass
    running = False
    for t in threads:
        t.join()
