# TODO: Clean up inheritance relationships

from enum import Enum
from typing import Dict, Any

from ..protocol_setup_form import (ProtocolSetupForm, ProtocolSetupFormItem,
                                   ProtocolSetupFormItems)
from ..protocol_script_builder import (CciNormalizationScriptBuilder,
                                       CciCountOnlyScriptBuilder,
                                       CciPresuspNormScriptBuilder,
                                       CciNormalization4x96ScriptBuilder)
from .setup_common import (CommonSetupFormItems, ColRangeSetupFormItems,
                           SplittingCommonSetupFormItems)


class CciCommonSetupFormItems:
    def init_items(self):
        self.add_item(
            "Flow cell ID",
            config_var='logging.run_info.flowcell_id',
            type_=self.ItemType.STRING
        )


class PresuspSetupFormItems:
    def init_items(self):
        self.add_item(
            "Source plate well fill volume (μL)",
            config_var='cell_splitting.presusp_well_vol',
            type_=self.ItemType.FLOAT,
            min_=20.,
            max_=300.,
            default=200.
        )

    @classmethod
    def transmute_vars(cls, temp_vars: Dict[str, Any],
                       config_vars: Dict[str, Any]):
        start_col = config_vars['cell_splitting.start_col']
        end_col = config_vars['cell_splitting.end_col']
        fill_vol = config_vars['cell_splitting.presusp_well_vol']
        well_names = [
            f'{row_name}{col_no}'
            for col_no in range(start_col, end_col + 1)
            for row_name in "ABCDEFGH"]
        config_vars['liquid_tracking.initial_fills.source_plate'] = [
            (well_name, 'old_media', fill_vol)
            for well_name in well_names
            ] + [
            (well_name, 'cells', 1e-3)
            for well_name in well_names
            ]


class CciNormalizationSetupFormItems(ProtocolSetupFormItems,
                                     CciCommonSetupFormItems,
                                     SplittingCommonSetupFormItems):
    class TargetEntryMode(Enum):
        UNIFORM = "Use uniform target count for all wells"
        TPW = "Use 96 individual per-well targets"

    def init_items(self):
        SplittingCommonSetupFormItems.init_items(self)
        CciCommonSetupFormItems.init_items(self)

        self.add_item(
            "Mode",
            temp_var='target_entry_mode',
            type_=self.ItemType.STRING,
            choices=[x.value for x in self.TargetEntryMode],
        )
        self.add_item(
            "Main plate seeding target count (× 1k cells/well)",
            temp_var='target_count_uniform',
            type_=self.ItemType.FLOAT,
            min_=0.,
            max_=100.,
            default=20.
        )
        self.add_item(
            "Main plate seeding target counts (× 1k cells/well)",
            temp_var='target_counts_tpw',
            type_=self.ItemType.TABLE,
            table_itemtype=self.ItemType.FLOAT,
            table_cols=[str(x) for x in range(1, 13)],
            table_rows="ABCDEFGH",
            min_=0.,
            max_=100.,
            default=0.
        )


    @classmethod
    def transmute_vars(cls, temp_vars: Dict[str, Any],
                       config_vars: Dict[str, Any]):
        SplittingCommonSetupFormItems.transmute_vars(temp_vars, config_vars)
        if cls.TargetEntryMode(temp_vars['target_entry_mode']) \
                == cls.TargetEntryMode.UNIFORM:
            target_counts = {
                f'{row}{col}': temp_vars['target_count_uniform'] * 1e3
                for row in "ABCDEFGH"
                for col in range(1, 13)
            }
        else:
            target_counts = {
                f'{row}{col}':
                    temp_vars['target_counts_tpw'][(row, str(col))] * 1e3
                for row in "ABCDEFGH"
                for col in range(1, 13)
            }
        config_vars['cell_splitting.target_counts.stock_plate'] = target_counts


class CciPresuspNormSetupFormItems(CciNormalizationSetupFormItems,
                                   PresuspSetupFormItems):
    def init_items(self):
        CciNormalizationSetupFormItems.init_items(self)
        PresuspSetupFormItems.init_items(self)

    @classmethod
    def transmute_vars(cls, temp_vars: Dict[str, Any],
                       config_vars: Dict[str, Any]):
        CciNormalizationSetupFormItems.transmute_vars(temp_vars, config_vars)
        PresuspSetupFormItems.transmute_vars(temp_vars, config_vars)


class CciCountOnlySetupFormItems(ProtocolSetupFormItems,
                                 CommonSetupFormItems,
                                 ColRangeSetupFormItems,
                                 PresuspSetupFormItems,
                                 CciCommonSetupFormItems):
    def init_items(self):
        CommonSetupFormItems.init_items(self)
        ColRangeSetupFormItems.init_items(self)
        CciCommonSetupFormItems.init_items(self)
        PresuspSetupFormItems.init_items(self)

    @classmethod
    def transmute_vars(cls, temp_vars: Dict[str, Any],
                       config_vars: Dict[str, Any]):
        PresuspSetupFormItems.transmute_vars(temp_vars, config_vars)
        config_vars['cell_splitting.target_counts'] = {}
        config_vars['cell_splitting.dest_plate_defs'] = {}
        config_vars['cell_splitting.dest_plates'] = []


class CciNormalizationSetupForm(ProtocolSetupForm):
    form_items_cls = CciNormalizationSetupFormItems
    builder_cls = CciNormalizationScriptBuilder

    def init_form(self):
        self.hide_item(temp_var='target_counts_tpw')

    def on_item_change(self, item: ProtocolSetupFormItem, value: Any):
        if item.temp_var == 'target_entry_mode':
            enum_cls = self.form_items_cls.TargetEntryMode
            if enum_cls(value) == enum_cls.TPW:
                self.hide_item(temp_var='target_count_uniform')
                self.unhide_item(temp_var='target_counts_tpw')
            else:
                self.hide_item(temp_var='target_counts_tpw')
                self.unhide_item(temp_var='target_count_uniform')


class CciPresuspNormSetupForm(CciNormalizationSetupForm):
    form_items_cls = CciPresuspNormSetupFormItems
    builder_cls = CciPresuspNormScriptBuilder


class CciCountOnlySetupForm(ProtocolSetupForm):
    form_items_cls = CciCountOnlySetupFormItems
    builder_cls = CciCountOnlyScriptBuilder


class CciNormalization4x96SetupFormItems(ProtocolSetupFormItems,
                                         CciCommonSetupFormItems):

    def init_items(self):
        CciCommonSetupFormItems.init_items(self)

        self.add_item(
            "Imaging plate seeding target (× 1k cells/well)",
            temp_var='target_count_imaging',
            type_=self.ItemType.FLOAT,
            min_=0.,
            max_=100.,
            default=20.
        )
        self.add_item(
            "Seeding targets for remaining plates (× 1k cells/well)",
            temp_var='target_count_other',
            type_=self.ItemType.FLOAT,
            min_=0.,
            max_=100.,
            default=10.,
        )

    @classmethod
    def transmute_vars(cls, temp_vars: Dict[str, Any],
                       config_vars: Dict[str, Any]):
        config_vars[f'cell_splitting.target_counts.glass_plate'] = {
            f'{row}{col}': temp_vars['target_count_imaging'] * 1e3
            for row in "ABCDEFGH"
            for col in range(1, 13)
        }

        secondary_target_counts = {
            f'{row}{col}': temp_vars['target_count_other'] * 1e3
            for row in "ABCDEFGH"
            for col in range(1, 13)
        }
        config_vars[f'cell_splitting.target_counts.freeze_plate'] = secondary_target_counts
        config_vars[f'cell_splitting.target_counts.seq_plate'] = secondary_target_counts
        config_vars[f'cell_splitting.target_counts.backup_plate'] = secondary_target_counts


class CciNormalization4x96SetupForm(ProtocolSetupForm):
    form_items_cls = CciNormalization4x96SetupFormItems
    builder_cls = CciNormalization4x96ScriptBuilder
