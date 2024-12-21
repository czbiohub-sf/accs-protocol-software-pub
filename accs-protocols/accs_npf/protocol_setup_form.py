from abc import ABC, abstractmethod
from collections import OrderedDict
import datetime
from enum import Enum
import json
from pathlib import Path
from typing import Any, Dict, Optional, Sequence, TextIO, Union

import IPython.display
import ipysheet
import ipywidgets

from .protocol_script_builder import (ProtocolScriptBuilder,
                                     TipcullingProtocolScriptBuilder)
from . import paths


class ProtocolSetupFormItem:
    class ItemType(Enum):
        INT = 'int'
        FLOAT = 'float'
        BOOL = 'bool'
        STRING = 'string'
        TABLE = 'table'

    def __init__(
        self,
        desc: str,
        type_: Optional[Union[ItemType, str]] = ItemType.STRING,
        config_var: Optional[str] = None,
        temp_var: Optional[str] = None,
        default: Any = None,
        min_: Optional[float] = None,
        max_: Optional[float] = None,
        choices: Optional[Sequence[Any]] = None,  # TODO: Fix typehint
        alias: Optional[str] = None,
        table_itemtype: Optional[Union[ItemType, str]] = None,
        table_rows: Optional[Sequence[str]] = None,
        table_cols: Optional[Sequence[str]] = None,
            ):
        type_ = self.ItemType(type_)
        if config_var is None and temp_var is None:
            raise ValueError("either config_var or temp_var must be non-None")
        self.type_ = self.ItemType(type_)
        if table_itemtype is not None:
            table_itemtype = self.ItemType(table_itemtype)
        self.table_itemtype = table_itemtype
        self.desc = desc
        self.config_var = config_var
        self.temp_var = temp_var
        self.default = default
        self.min_ = min_
        self.max_ = max_
        self.choices = choices
        self.alias = alias
        self.table_rows = \
            tuple(table_rows) if table_rows is not None else None
        self.table_cols = \
            tuple(table_cols) if table_cols is not None else None

    def convert_value(self, val: Any, empty_none: bool = False) -> Any:
        type_var = \
            self.table_itemtype if self.type_ == self.ItemType.TABLE \
            else self.type_
        if empty_none and not str(val).strip():
            return None
        if type_var == self.ItemType.INT:
            return int(val)
        elif type_var == self.ItemType.FLOAT:
            return float(val)
        elif type_var == self.ItemType.BOOL:
            return bool(val)
        elif type_var == self.ItemType.STRING:
            return str(val)
        assert False


class ProtocolSetupFormItems(ABC, Sequence[ProtocolSetupFormItem]):
    Item = ProtocolSetupFormItem
    ItemType = Item.ItemType
    # TODO: if config_var is given, take default from default_config;
    # otherwise it can be specified with a parameter to add_item()

    def __init__(self):
        self._items: List[ProtocolSetupFormItem] = []
        self._items_by_alias: Dict[str, ProtocolSetupFormItem] = {}
        self._items_by_temp_var: Dict[str, ProtocolSetupFormItem] = {}
        self._items_by_config_var: Dict[str, ProtocolSetupFormItem] = {}
        self.init_items()

    @abstractmethod
    def init_items(self):
        ...

    def add_item(self, *args, **kwargs):
        item = self.Item(*args, **kwargs)
        self._items.append(item)
        if item.alias is not None:
            self._items_by_alias[item.alias] = item
        if item.temp_var is not None:
            self._items_by_temp_var[item.temp_var] = item
        if item.config_var is not None:
            self._items_by_config_var[item.config_var] = item

    def get_item(self, alias: Optional[str] = None,
                 temp_var: Optional[str] = None,
                 config_var: Optional[str] = None):
        if alias is not None:
            return self._items_by_alias[alias]
        elif temp_var is not None:
            return self._items_by_temp_var[temp_var]
        elif config_var is not None:
            return self._items_by_config_var[config_var]
        raise ValueError("must specify alias, temp_var, or config_var")

    @classmethod
    def transmute_vars(cls, temp_vars: Dict[str, Any],
                       config_vars: Dict[str, Any]):
        pass

    def __len__(self):
        return len(self._items)

    def __getitem__(self, i):
        return self._items[i]


class LabeledSheetWidget(ipywidgets.VBox):
    def __init__(self, description: str, row_names: Sequence[str],
                 col_names: Sequence[str]):
        self.row_names = tuple(row_names)
        self.col_names = tuple(col_names)
        n_rows = len(self.row_names)
        n_cols = len(self.col_names)
        cells = tuple(ipysheet.Cell(column_start=col_i, column_end=col_i,
                                    row_start=row_i, row_end=row_i, value=0.0)
                      for col_i in range(n_cols) for row_i in range(n_rows))
        self.label = ipywidgets.Label(description)
        self.sheet = ipysheet.Sheet(cells=cells, rows=n_rows, columns=n_cols,
                                    row_headers=self.row_names,
                                    column_headers=self.col_names)
        super().__init__([self.label, self.sheet])
        self.layout.width = "80%"  # TODO do this better


class ProtocolSetupForm:
    form_items_cls = ProtocolSetupFormItems
    builder_cls = ProtocolScriptBuilder

    def __init__(self, form_items: Optional[ProtocolSetupFormItems] = None):
        self.param_widgets = [] # TODO: Type hint
        self.form_items = \
            form_items if form_items is not None else self.form_items_cls()
        self.builder = self.builder_cls()
        self._temp_var_widgets = {}
        self._config_var_widgets = {}
        self._item_widgets = {}
        self._output_dir_widget = None
        self._main_output_path_widget = None
        self._copy_button = None
        self._main_output_path = ""
        self._layout_display = {}
        self.reset_run_id()
        for item in self.form_items:
            kwargs = self._get_default_widget_kwargs()
            if item.default is not None:
                kwargs['value'] = item.default
            if item.type_ == item.ItemType.INT:
                widget_cls = \
                    ipywidgets.BoundedIntText \
                    if any(x is not None for x in (item.min_, item.max_)) \
                    else ipywidgets.IntText
                w = widget_cls(
                    description=item.desc,
                    min=item.min_,
                    max=item.max_,
                    **kwargs
                    # TODO
                    )
            elif item.type_ == item.ItemType.FLOAT:
                widget_cls = \
                    ipywidgets.BoundedFloatText \
                    if any(x is not None for x in (item.min_, item.max_)) \
                    else ipywidgets.FloatText
                w = widget_cls(
                    description=item.desc,
                    min=item.min_,
                    max=item.max_,
                    **kwargs
                    # TODO
                    )
            elif item.type_ == item.ItemType.BOOL:
                w = ipywidgets.Checkbox(
                    description=item.desc,
                    **kwargs,
                    # TODO
                    )
            elif item.type_ == item.ItemType.TABLE:
                w = LabeledSheetWidget(
                    description=item.desc,
                    col_names=item.table_cols,
                    row_names=item.table_rows,
                    )
            else:
                if item.choices is not None:
                    w = ipywidgets.Dropdown(
                        description=item.desc,
                        options=item.choices,
                        **kwargs,
                        # TODO
                        )
                else:
                    w = ipywidgets.Text(
                        description=item.desc,
                        **kwargs,
                        # TODO
                        )
            self._item_widgets[item] = w
            if item.config_var is not None:
                self._config_var_widgets[item.config_var] = w
            if item.temp_var is not None:
                self._temp_var_widgets[item.temp_var] = w
            w.observe(self._on_value_change, names='value')
            self.param_widgets.append(w)
        self.init_form()

    def get_header_widgets(self):
        return None

    def init_form(self):
        pass

    def get_widget(self, *args, **kwargs):
        item = self.form_items.get_item(*args, **kwargs)
        return self._item_widgets[item]

    def hide_item(self, *args, **kwargs):
        item = self.form_items.get_item(*args, **kwargs)
        widget = self._item_widgets[item]
        self._layout_display[item] = widget.layout.display
        self.get_widget(*args, **kwargs).layout.display = "none"

    def unhide_item(self, *args, **kwargs):
        item = self.form_items.get_item(*args, **kwargs)
        widget = self._item_widgets[item]
        widget.layout.display = \
            self._layout_display.get(item)

    def on_item_change(self, item: ProtocolSetupFormItem, value: Any):
        pass

    def _on_value_change(self, info: Dict[str, Any]):
        if info['name'] != "value":
            return
        widget = info['owner']
        new_value = info['new']
        item = {y: x for (x, y) in self._item_widgets.items()}[widget]
        self.on_item_change(item=item, value=new_value)

    def get_params_form_widget(self):
        return ipywidgets.VBox(self.param_widgets)

    def _get_default_widget_kwargs(self):
        return {
            'layout': ipywidgets.Layout(flex='0 1 auto', width='500px'),
            'style': {'description_width': 'initial'}}

    def get_output_form_widget(self):
        output_dir = paths.get_protocol_scripts_dir().resolve()
        self._output_dir_widget = ipywidgets.Text(
            description="Output dir", value=str(output_dir),
            **self._get_default_widget_kwargs())
        self._main_output_path_widget = ipywidgets.HTML("(n/a)")
        generate_button = ipywidgets.Button(description="Generate script")
        generate_button.on_click(self._on_generate_button_click)
        self._copy_button = ipywidgets.Button(
            description="Copy path", disabled=True)
        self._copy_button.on_click(self._on_copy_button_click)
        self._log_output_widget = ipywidgets.Output()
        return ipywidgets.VBox([
            self._output_dir_widget,
            generate_button,
            ipywidgets.HBox([
                ipywidgets.Label("Path to protocol script:"),
                self._main_output_path_widget
                ]),
            self._copy_button,
            ipywidgets.HTML('<input id=\"cbholder\" type=\"text\" hidden/>'),
            self._log_output_widget
            ])

    def get_form(self):
        header_widgets = self.get_header_widgets() or []
        return header_widgets + [
            IPython.display.Markdown("## Enter run parameters"),
            self.get_params_form_widget(),
            IPython.display.Markdown("## Generate script"),
            self.get_output_form_widget()
        ]

    def display(self):
        for x in self.get_form():
            IPython.display.display(x)

    def get_item_value(self, *args, **kwargs):
        item = self.form_items.get_item(*args, **kwargs)
        if item.type_ == item.ItemType.TABLE:
            sheet = self._item_widgets[item].sheet
            return {
                (row, col): item.convert_value(sheet[row_idx, col_idx].value,
                                               empty_none=True)
                for (row_idx, row) in enumerate(sheet.row_headers)
                for (col_idx, col) in enumerate(sheet.column_headers)}
        else:
            return item.convert_value(self._item_widgets[item].value)

    def reset_run_id(self):
        self._run_id = None

    def get_run_id(self):
        if self._run_id is None:
            date_desc = datetime.datetime.now().strftime("%y%m%d-%H%M%S")
            self._run_id = f"{date_desc}-{self.builder.protocol_name}"
        return self._run_id

    def get_runconfig_items(self):
        temp_vars = {
            k: self.get_item_value(temp_var=k)
            for k in self._temp_var_widgets}
        config_vars = {
            k: self.get_item_value(config_var=k)
            for k in self._config_var_widgets}
        self.form_items.transmute_vars(config_vars=config_vars,
                                       temp_vars=temp_vars)
        config_vars['logging.run_id'] = self.get_run_id()
        return config_vars.items()

    def get_output_path(self, run_id: Optional[str] = None):
        output_dir = Path(self._output_dir_widget.value).resolve()
        if run_id is None:
            run_id = self.get_run_id()
        filename = f"{run_id}.py"
        return output_dir / filename

    def _on_generate_button_click(self, _):
        self._copy_button.disabled = True
        self._main_output_path_widget.value = "(n/a)"
        self._main_output_path_widget.disabled = True
        self.reset_run_id()
        output_path = self.get_output_path()
        try:
            self.generate_main_script(output_path)
        except FileNotFoundError as e:
            self._log_output_widget.append_stderr(
                f"FAILED: Invalid output directory?")
            return
        except Exception as e:
            self._log_output_widget.append_stderr(f"FAILED: {e!r}")
            return
        self._main_output_path = str(output_path)
        self._main_output_path_widget.value = (
            f"<code>{self._main_output_path}</code>")
        self._main_output_path_widget.disabled = False
        self._copy_button.disabled = False
        with self._log_output_widget:
            IPython.display.display(IPython.display.Markdown(
                "*Successfully generated protocol script.*"))

    def _on_copy_button_click(self, _):
        with self._log_output_widget:
            IPython.display.display(IPython.display.Javascript(f"""
                var cb = document.getElementById("cbholder");
                cb.value = {json.dumps(self._main_output_path)};
                cb.style.display="block";
                cb.select();
                document.execCommand("copy");
                cb.style.display="none";
                """))
            IPython.display.display(IPython.display.Markdown(
                "*Copied protocol script path to clipboard.*"))

    def generate_main_script(self, path: Union[Path, str]):
        path = Path(path)
        self.builder.reset_run_config()
        for dotpath, val in self.get_runconfig_items():
            self.builder.set_runconfig_param_by_dotpath(dotpath, val)
        self.builder.write_script(path)
        self.reset_run_id()


class TipculledProtocolSetupForm(ProtocolSetupForm):
    SPLIT_FACTOR_DEFN_PLATE = "stock_plate"
    tipculling_builder_cls = TipcullingProtocolScriptBuilder

    def init_form(self):
        self._tipcull_output_path_widget = ipywidgets.HTML("(n/a)")
        self._tipcull_copy_button = ipywidgets.Button(
            description="Copy path", disabled=True)
        self._tipcull_copy_button.on_click(self._on_tipcull_copy_button_click)
        self._disable_tipcull_output_widgets()

    def _enable_tipcull_output_widgets(self):
        self._tipcull_copy_button.disabled = False
        self._tipcull_output_path_widget.disabled = False

    def _disable_tipcull_output_widgets(self):
        self._tipcull_copy_button.disabled = True
        self._tipcull_output_path_widget.value = "(n/a)"
        self._tipcull_output_path_widget.disabled = True

    def _on_generate_button_click(self, *args):
        self._disable_tipcull_output_widgets()
        super()._on_generate_button_click(*args)
        if not self.get_item_value(
                    config_var='cell_splitting.cherryskipping_mode'):
            return
        self._tipcull_output_path = str(self.get_output_path(
            self.get_tipcull_run_id()))
        with self._log_output_widget:
            IPython.display.display(IPython.display.Markdown(
                "*Generating tip culling script...*"))
        self._tipcull_output_path_widget.value = "(...)"
        self.generate_tipcull_script(self._tipcull_output_path)
        with self._log_output_widget:
            IPython.display.display(IPython.display.Markdown(
                "*Successfully generated tip culling script.*"))
        self._tipcull_output_path_widget.value = (
            f"<code>{self._tipcull_output_path}</code>")
        self._enable_tipcull_output_widgets()

    def generate_tipcull_script(self, path: Union[Path, str]):
        path = Path(path)
        split_factors = dict(self.get_runconfig_items())[
            f'cell_splitting.target_split_factors.{self.SPLIT_FACTOR_DEFN_PLATE}']
        with open(self._main_output_path, "r") as f:
            tipculling_builder = self.tipculling_builder_cls(
                f, split_factors, run_id=self.get_tipcull_run_id(),)
        tipculling_builder.write_script(path)

    def get_tipcull_output_path(self):
        return self.get_output_path(self.get_tipcull_run_id())

    def get_tipcull_run_id(self):
        main_run_id = self.get_run_id()
        return f"{main_run_id}-tip_culling"

    def get_output_form_widget(self):
        prev = super().get_output_form_widget()
        main_widgets = list(prev.children)[:-1]
        return ipywidgets.VBox(main_widgets + [
            ipywidgets.HBox([
                ipywidgets.Label("Path to tip culling script:"),
                self._tipcull_output_path_widget
                ]),
            self._tipcull_copy_button,
            self._log_output_widget
            ])

    def _on_tipcull_copy_button_click(self, _):
        with self._log_output_widget:
            IPython.display.display(IPython.display.Javascript(f"""
                var cb = document.getElementById("cbholder");
                cb.value = {json.dumps(self._tipcull_output_path)};
                cb.style.display="block";
                cb.select();
                document.execCommand("copy");
                cb.style.display="none";
                """))
            IPython.display.display(IPython.display.Markdown(
                "*Copied tip culling script path to clipboard.*"))
