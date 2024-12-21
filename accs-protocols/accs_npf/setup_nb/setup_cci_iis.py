from typing import Dict, Any

import IPython.display

from ..protocol_script_builder import CciInfImgSplitScriptBuilder
from ..protocol_setup_form import ProtocolSetupForm, ProtocolSetupFormItems
from .setup_common import CommonSetupFormItems, PLATE_TYPES
from .setup_cci_normalization import CciCommonSetupFormItems


class CciInfImgSplitSetupFormItems(ProtocolSetupFormItems,
                                   CommonSetupFormItems,
                                   CciCommonSetupFormItems):
    def init_items(self):
        CommonSetupFormItems.init_items(self)
        CciCommonSetupFormItems.init_items(self)

        self.add_item(
            "Stock plate ID",
            config_var='logging.run_info.stock_plate_id',
            type_=self.ItemType.STRING
        )
        self.add_item(
            "Imaging plate A ID",
            config_var='logging.run_info.img_plate_a_id',
            type_=self.ItemType.STRING
        )
        self.add_item(
            "Imaging plate B ID",
            config_var='logging.run_info.img_plate_b_id',
            type_=self.ItemType.STRING
        )
        self.add_item(
            "Stock plate seeding target count (× 1k cells/well)",
            temp_var='target_count_stock',
            type_=self.ItemType.FLOAT,
            min_=0.,
            max_=100.,
            default=10.
        )
        for (group_name, group_key) in (
                ("A", "a"), ("A'", "aprime"), ("B", "b"), ("B'", "bprime")):
            self.add_item(
                f"Group {group_name} target count (× 1k cells/well)",
                temp_var=f'target_count_{group_key}',
                type_=self.ItemType.FLOAT,
                min_=0.,
                max_=100.,
                default=4.5
            )

    @classmethod
    def transmute_vars(cls, temp_vars: Dict[str, Any],
                       config_vars: Dict[str, Any]):
        def fill_target_counts(form_val, col_nos):
            return {
                f'{row}{col}': form_val * 1e3
                for row in "ABCDEFGH"
                for col in col_nos}
        config_vars['cell_splitting.target_counts.stock_plate'] \
            = fill_target_counts(temp_vars['target_count_stock'], range(1, 13))
        for group_ltr in ("a", "b"):
            target_counts = {}
            target_counts.update(
                fill_target_counts(
                    temp_vars[f'target_count_{group_ltr}'], range(1, 7)))
            target_counts.update(
                fill_target_counts(
                    temp_vars[f'target_count_{group_ltr}prime'], range(7, 13)))
            config_vars[
                f'cell_splitting.target_counts.img_plate_{group_ltr}'
                ] = target_counts


class CciInfImgSplitSetupForm(ProtocolSetupForm):
    form_items_cls = CciInfImgSplitSetupFormItems
    builder_cls = CciInfImgSplitScriptBuilder

    def get_header_widgets(self):
        return [IPython.display.Image(
            filename="form_media/infection_imaging_illustration.png")]
