import http.client
import os
import random
import shlex
import shutil
import subprocess
import threading
import time
import unittest
from contextlib import contextmanager


CALL_PER_THREAD = 50
REQ_PER_CALL = 50
THREADS_NUM = 4


class PropagatingThread(threading.Thread):
    def run(self):
        self.exc = None
        try:
            self.ret = self._target(*self._args, **self._kwargs)
        except BaseException as e:
            self.exc = e

    def join(self, timeout=None):
        super(PropagatingThread, self).join(timeout)
        if self.exc:
            raise self.exc
        return self.ret


@contextmanager
def http_conn(*args, **kwds):
    conn = http.client.HTTPConnection(*args, **kwds)
    try:
        yield conn
    finally:
        conn.close()

def etcd_get(self, conn):
    conn.request("GET", "/etcd_get")
    response = conn.getresponse()
    self.assertEqual(response.status, 200)
    res = response.read().rstrip().decode()
    self.assertEqual(res, "k v")
    # give a bit randomness
    time.sleep(random.uniform(0, 0.01))

def timeout(self, conn):
    conn.request("GET", "/timeout")
    response = conn.getresponse()
    self.assertEqual(response.status, 200)
    res = response.read().rstrip().decode()
    self.assertEqual(res, "failed to call: timeout")

def run_in_thread(self, f):
    def _f():
        for i in range(CALL_PER_THREAD):
            with http_conn("127.0.0.1", port=6666) as conn:
                for i in range(REQ_PER_CALL):
                    f(self, conn)
    return _f

def bombard(self, f):
    th = [PropagatingThread(target=run_in_thread(self, f)) for i in range(THREADS_NUM)]
    for t in th:
        t.start()
    for t in th:
        t.join()


class StressTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        subprocess.check_call(shlex.split("etcdctl put k v"))
        log_dir = "t/stress/logs"
        if os.path.exists(log_dir):
            shutil.rmtree(log_dir)
        os.mkdir(log_dir)
        pwd = os.getcwd()
        # avoid using builtin Nginx in the CI env
        cls.nginx = subprocess.Popen(shlex.split(f"/usr/local/openresty/nginx/sbin/nginx -p {pwd}/t/stress -c conf/nginx.conf"))
        time.sleep(1)

    @classmethod
    def tearDownClass(cls):
        nginx = cls.nginx
        nginx.terminate()
        nginx.wait()

    def test_etcd_get(self):
        bombard(self, etcd_get)

    def test_timeout(self):
        bombard(self, timeout)

    def test_server_stream(self):
        self.count = 20
        self.turn = 100

        def watch(self, i):
            def _f():
                with http_conn("127.0.0.1", port=6666) as conn:
                    conn.request("GET", f"/etcd_watch?key={i}&count={self.turn+1}")
                    resp = conn.getresponse()
                    self.barrier.wait()
                    if resp.status != 200:
                        res = resp.read().rstrip().decode()
                        print(res)
                    self.assertEqual(resp.status, 200)
                    res = resp.read().rstrip().decode()
                    exp = []
                    for j in range(self.turn):
                        if j % 2 == 0:
                            v = j * 10 + i
                            exp.append(f"PUT {i} {v}")
                        else:
                            exp.append(f"DELETE {i}")
                    self.assertEqual("\n".join(exp), res)
            return _f

        def write(self):
            def _f():
                self.barrier.wait()
                with http_conn("127.0.0.1", port=6666) as conn:
                    for i in range(self.turn):
                        for j in range(self.count):
                            if i % 2 == 0:
                                conn.request("GET", f"/etcd_put?key={j}&value={i*10+j}")
                            else:
                                conn.request("GET", f"/etcd_delete?key={j}")
                            resp = conn.getresponse()
                            self.assertEqual(resp.status, 200)
                            resp.read()
                            # give a bit randomness
                            time.sleep(random.uniform(0, 0.01))
            return _f

        self.barrier = threading.Barrier(self.count + 1, timeout = 30)
        th = [PropagatingThread(target=watch(self, i)) for i in range(self.count)]
        for t in th:
            t.start()
        w_th = PropagatingThread(target=write(self))
        w_th.start()
        w_th.join()
        for t in th:
            t.join()


if __name__ == '__main__':
    unittest.main()
