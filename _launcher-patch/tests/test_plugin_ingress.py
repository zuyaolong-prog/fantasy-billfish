import importlib.util
from pathlib import Path
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location("deliver_plugin", Path(__file__).resolve().parents[1] / "deliver-plugin-package.py")
delivery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(delivery)


class PluginDeliveryTests(unittest.TestCase):
    def test_host_readiness_then_one_install_and_settings(self):
        client = Mock()
        client.call.side_effect = [delivery.ApiError("online_unavailable", "Starting"), {}, {"id": "plugin"}, {}]
        with patch.object(delivery.time, "sleep"):
            result = delivery.deliver(client, "插件 包.eagleplugin")
        self.assertEqual([call.args[0] for call in client.call.call_args_list],
                         ["host.info", "host.info", "plugins.install", "ui.settings.open"])
        install = client.call.call_args_list[2]
        self.assertEqual(install.args[1], {"source_path": "插件 包.eagleplugin"})
        self.assertEqual(install.kwargs["request_id"], result["job_id"])

    def test_install_timeout_is_not_retried_and_has_queryable_job(self):
        client = Mock()
        client.call.side_effect = [{}, delivery.ApiError("online_timeout", "Timeout")]
        with self.assertRaises(delivery.ApiError) as caught:
            delivery.deliver(client, "p.eagleplugin")
        self.assertEqual(client.call.call_count, 2)
        self.assertEqual(caught.exception.details["job_id"], client.call.call_args.kwargs["request_id"])

    def test_ui_failure_reports_successful_install_without_reinstall(self):
        client = Mock()
        client.call.side_effect = [{}, {"id": "installed"}, delivery.ApiError("online_timeout", "Timeout")]
        with self.assertRaises(delivery.ApiError) as caught:
            delivery.deliver(client, "p.eagleplugin")
        self.assertEqual(caught.exception.code, "plugin_installed_ui_unavailable")
        self.assertEqual(caught.exception.details["installation"], {"id": "installed"})
        self.assertEqual(client.call.call_count, 3)

    def test_host_timeout_never_sends_install(self):
        client = Mock()
        client.call.side_effect = delivery.ApiError("online_timeout", "Unavailable")
        with patch.object(delivery.time, "monotonic", side_effect=[0, 0, 2]):
            with self.assertRaises(delivery.ApiError):
                delivery.deliver(client, "p.eagleplugin", ready_timeout=1)
        self.assertEqual(client.call.call_count, 1)


if __name__ == "__main__":
    unittest.main()
