# stikcommon/e2e/logmw.py —— wsgidav 中间件：把 "METHOD path" 追加到 $DAVSYNC_LOG。
#
# 供 e2e 断言「动词/路径序列」（OPTIONS 先于 PROPFIND、GET 先于 PUT 等）。
# wsgidav 以 (application, config) 实例化中间件；日志路径从环境变量取，
# 避免把自定义项塞进 wsgidav 的 config。
import os


class LogMw:
    def __init__(self, wsgidav_app, next_app, config):
        self.application = next_app
        self.path = os.environ.get("DAVSYNC_LOG", "/tmp/opencode/davsync_access.log")

    def __call__(self, environ, start_response):
        try:
            with open(self.path, "a") as f:
                f.write("%s %s\n" % (environ.get("REQUEST_METHOD"),
                                     environ.get("PATH_INFO")))
        except Exception:
            pass
        return self.application(environ, start_response)
