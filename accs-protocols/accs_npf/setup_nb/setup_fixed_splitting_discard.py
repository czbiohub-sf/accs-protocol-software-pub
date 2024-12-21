from typing import Dict, Any


from ..protocol_setup_form import (TipculledProtocolSetupForm,
                                   ProtocolSetupFormItems)
from ..protocol_script_builder import FixedDiscardSplittingScriptBuilder
from .setup_common import (CommonSetupFormItems,
                           CherryskippingSetupFormItems)


class FixedDiscardSplittingSetupFormItems(ProtocolSetupFormItems,
                                          CommonSetupFormItems,
                                          CherryskippingSetupFormItems):
    def init_items(self):
        CommonSetupFormItems.init_items(self)

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
        split_factors = {
            f'{row}{col}':
                temp_vars['target_splits_tpw'][(row, str(col))]
            for row in "ABCDEFGH"
            for col in range(1, 13)
        }
        config_vars['cell_splitting.target_split_factors.source_plate'] \
            = split_factors


class FixedDiscardSplittingSetupForm(TipculledProtocolSetupForm):
    SPLIT_FACTOR_DEFN_PLATE = "source_plate"
    form_items_cls = FixedDiscardSplittingSetupFormItems
    builder_cls = FixedDiscardSplittingScriptBuilder
