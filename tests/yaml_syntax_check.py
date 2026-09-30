import sys
import yaml
from pathlib import Path


def _include(loader, node):
    """Resolve `!include path.yaml` relative to the INCLUDING file's directory,
    mirroring ESPHome (yaml_util.py IncludeFile.load uses parent_file.parent).
    Recursively loads the target with the same loader class.
    """
    file = str(loader.construct_scalar(node))
    here = Path(loader.stream.name).parent
    target = (here / file).resolve()
    with target.open(encoding='utf-8') as fh:
        return next(yaml.load_all(fh, Loader=L))


def _construct_yaml_map(loader, node):
    """Mirror ESPHome's custom mapping constructor (yaml_util.py): merge keys
    (`<<`) are resolved through construct_object so `<<: !include x.yaml`
    receives the included mapping, with YAML's first-wins semantics.

    PyYAML's stock flatten_mapping inspects raw nodes and would reject an
    `!include` scalar as a merge value.
    """
    pairs = []
    merge_pairs = []
    seen = {}

    for key_node, value_node in node.value:
        if key_node.tag == 'tag:yaml.org,2002:merge':
            value = loader.construct_object(value_node)
            if isinstance(value, dict):
                merge_pairs.extend(value.items())
            elif isinstance(value, list):
                for item in value:
                    if not isinstance(item, dict):
                        raise yaml.constructor.ConstructorError(
                            'While constructing a mapping',
                            node.start_mark,
                            f'Expected a mapping for merging, but found {type(item)}',
                            value_node.start_mark,
                        )
                    merge_pairs.extend(item.items())
            else:
                raise yaml.constructor.ConstructorError(
                    'While constructing a mapping',
                    node.start_mark,
                    f'Expected a mapping or list of mappings for merging, but found {type(value)}',
                    value_node.start_mark,
                )
            continue

        key = str(loader.construct_object(key_node))
        value = loader.construct_object(value_node)
        if key in seen:
            raise yaml.constructor.ConstructorError(
                f'Duplicate key "{key}"',
                key_node.start_mark,
                'NOTE: Previous declaration here:',
                seen[key],
            )
        seen[key] = key_node.start_mark
        pairs.append((key, value))

    for key, value in merge_pairs:
        key = str(key)
        if key in seen:  # first-wins: explicit keys override merged ones
            continue
        pairs.append((key, value))
        seen[key] = None

    return dict(pairs)


class L(yaml.SafeLoader):
    pass


L.add_constructor('!include', _include)
# Override the default mapping constructor (ESPHome does the same)
L.add_constructor('tag:yaml.org,2002:map', _construct_yaml_map)

# Ignore all remaining ESPHome tags (!lambda, !secret, ...) - syntax check only
L.add_multi_constructor('', lambda loader, tag_suffix, node: tag_suffix)

files = [
    'esp-web-radio.yaml',
    'packages/lvgl_theme.yaml',
    'packages/display-fonts.yaml',
    'packages/esp-web-radio-lvgl_ui.yaml',
    'packages/esp-web-radio-page_now_playing.yaml',
    'packages/esp-web-radio-page_stations.yaml',
    'packages/esp-web-radio-page_ap_setup.yaml',
    'packages/esp-web-radio-offline_stations.yaml',
    'packages/esp-web-radio-homeassistant.yaml',
    'packages/common-colors.yaml',
    'packages/swipe_navigation.yaml',
]

ok = True
for f in files:
    try:
        with open(f, encoding='utf-8') as fh:
            list(yaml.load_all(fh, Loader=L))
        print('OK   ', f)
    except Exception as e:
        ok = False
        print('FAIL ', f, ':', e)

sys.exit(0 if ok else 1)