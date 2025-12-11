import pytest

from neutral_atom_vm.device import available_presets, build_device_from_config


def test_presets_expose_named_configuration_families():
    presets = available_presets()
    config = presets["local-cpu"]["ideal_small_array"]
    assert "configuration_families" in config
    families = config["configuration_families"]
    assert isinstance(families, dict)
    assert "default" in families
    default = families["default"]
    assert isinstance(default["site_ids"], list)
    assert default["site_ids"] == config["site_ids"]
    assert "regions" in default
    assert isinstance(default["regions"], list)
    assert default["regions"][0]["role"] == "DATA"


def test_device_applies_active_configuration_family():
    presets = available_presets()
    config = presets["local-cpu"]["ideal_small_array"]
    device = build_device_from_config(
        "local-cpu",
        profile="ideal_small_array",
        config=config,
    )
    assert device.configuration_families
    active_name = device.active_configuration_family_name
    assert isinstance(active_name, str)
    assert active_name in device.configuration_families
    active = device.configuration_families[active_name]
    assert list(active.site_ids) == list(device.site_ids)


def test_build_device_rejects_empty_site_ids():
    presets = available_presets()
    config = dict(presets["local-cpu"]["ideal_small_array"])
    config.pop("configuration_families", None)
    config["site_ids"] = []
    with pytest.raises(ValueError, match="at least one occupied site"):
        build_device_from_config(
            "local-cpu",
            profile="ideal_small_array",
            config=config,
        )
