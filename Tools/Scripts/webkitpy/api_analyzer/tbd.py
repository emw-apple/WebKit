
import yaml  # todo: not stdlib

class TBD(yaml.YAMLObject):
    yaml_tag = '!tapi-tbd'

    @property
    def install_name(self):
        return getattr(self, 'install-name')

    @classmethod
    def load_from(cls, file: str):
        return yaml.unsafe_load_all(open(file))

