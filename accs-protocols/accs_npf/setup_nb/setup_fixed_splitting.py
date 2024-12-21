from typing import Dict, Any


from ..protocol_setup_form import (ProtocolSetupForm, ProtocolSetupFormItems,
                                   TipculledProtocolSetupForm)
from ..protocol_script_builder import (FixedSplittingScriptBuilder,
                                       ColwiseFixedSplittingScriptBuilder)
from .setup_common import (SplittingCommonSetupFormItems,
                           CherryskippingSetupFormItems)


class FixedSplittingSetupFormItems(ProtocolSetupFormItems,
                                   SplittingCommonSetupFormItems,
                                   CherryskippingSetupFormItems):
    def init_items(self):
        SplittingCommonSetupFormItems.init_items(self)

        self.add_item(
            "Seeding split factors",
            temp_var='target_splits_tpw',
            type_=self.ItemType.TABLE,
            table_itemtype=self.ItemType.FLOAT,
            table_cols=[str(x) for x in range(1, 13)],
            table_rows="ABCDEFGH",
            min_=0.,
            max_=13.,
            default=0.
        )

        CherryskippingSetupFormItems.init_items(self)

    @classmethod
    def transmute_vars(cls, temp_vars: Dict[str, Any],
                       config_vars: Dict[str, Any]):
        SplittingCommonSetupFormItems.transmute_vars(temp_vars, config_vars)
        split_factors = {
            f'{row}{col}':
                temp_vars['target_splits_tpw'][(row, str(col))]
            for row in "ABCDEFGH"
            for col in range(1, 13)
        }
        config_vars['cell_splitting.target_split_factors.stock_plate'] \
            = split_factors


class FixedSplittingSetupForm(TipculledProtocolSetupForm):
    form_items_cls = FixedSplittingSetupFormItems
    builder_cls = FixedSplittingScriptBuilder


class ColwiseFixedSplittingSetupFormItems(ProtocolSetupFormItems,
                                          SplittingCommonSetupFormItems):
    def init_items(self):
        SplittingCommonSetupFormItems.init_items(self)

        self.add_item(
            "Seeding split factors",
            temp_var='target_splits_colwise',
            type_=self.ItemType.TABLE,
            table_itemtype=self.ItemType.FLOAT,
            table_cols=[str(x) for x in range(1, 13)],
            table_rows=["A-H"],
            min_=1.3,
            max_=13.,
            default=2.
        )

    @classmethod
    def transmute_vars(cls, temp_vars: Dict[str, Any],
                       config_vars: Dict[str, Any]):
        SplittingCommonSetupFormItems.transmute_vars(temp_vars, config_vars)
        split_factors = {
            f'A{col}': temp_vars['target_splits_colwise'][("A-H", str(col))]
            for col in range(1, 13)
        }
        config_vars['cell_splitting.target_split_factors.stock_plate'] \
            = split_factors
        config_vars['cell_splitting.trough_slot'] = 5


class ColwiseFixedSplittingSetupForm(ProtocolSetupForm):
    form_items_cls = ColwiseFixedSplittingSetupFormItems
    builder_cls = ColwiseFixedSplittingScriptBuilder
