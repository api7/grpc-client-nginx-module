import http.client
import os
import random
import shlex
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
            os.remove(log_dir)
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


if __name__ == '__main__':
    unittest.main()
