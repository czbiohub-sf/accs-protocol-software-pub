from typing import Dict, Any


from ..protocol_setup_form import ProtocolSetupForm, ProtocolSetupFormItems
from ..protocol_script_builder import Expansion96To8x12ScriptBuilder, ExpansionMs96To3x96ScriptBuilder
from .setup_common import CommonSetupFormItems


class Expansion96To8x12SetupFormItems(ProtocolSetupFormItems,
                                      CommonSetupFormItems):

    def init_items(self):
        CommonSetupFormItems.init_items(self)


class ExpansionMs96To3x96SetupFormItems(ProtocolSetupFormItems,
                                        CommonSetupFormItems):
    def init_items(self):
        CommonSetupFormItems.init_items(self)

        self.add_item(
            "'A' plate (slot 1, source cols 1-4) ID",
            config_var='logging.run_info.out_plate_a_id',
            type_=self.ItemType.STRING
        )
        self.add_item(
            "'B' plate (slot 2, source cols 5-8) ID",
            config_var='logging.run_info.out_plate_b_id',
            type_=self.ItemType.STRING
        )
        self.add_item(
            "'C' plate (slot 4, source cols 9-12) ID",
            config_var='logging.run_info.out_plate_c_id',
            type_=self.ItemType.STRING
        )


class Expansion96To8x12SetupForm(ProtocolSetupForm):
    form_items_cls = Expansion96To8x12SetupFormItems
    builder_cls = Expansion96To8x12ScriptBuilder


class ExpansionMs96To3x96SetupForm(ProtocolSetupForm):
    form_items_cls = ExpansionMs96To3x96SetupFormItems
    builder_cls = ExpansionMs96To3x96ScriptBuilder