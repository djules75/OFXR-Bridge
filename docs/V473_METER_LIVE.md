# V473 Elliottate live display fix

Base sealed V472 95b667ba04044d612686540277235d2ab4b04e49. Owner reports that selecting the Elliottate display never completes Applying settings.

Root cause: desktop/F10 and the renderer support display values 5/6, but embedded_control.cpp still accepted only values 0..4. apply_runtime_settings returned early at set_overlay_setting, leaving applied_runtime_revision unacknowledged. The INI stayed on the previously selected corner; later logging, NR and R19 application could also be delayed while this invalid display selection remained desired.

The runtime codec now accepts elliott_controller and elliott_bottom and derives read/write bounds from its single names array. Existing Off/corner values and default upper_right are unchanged; invalid values remain rejected. No forced acknowledgement or fake status is added: the normal application completes and publishes its revision.

Regression coverage now requests each mode through the actual autonomous runtime adapter and embedded INI writer instead of writing the INI directly. With the old production codec, both D3D11/D3D12 meter fixtures fail at the application acknowledgement. After repair they must verify runtime readback, shared status receipt, unchanged frame-generation revision, actual meter upload and controller/bottom/corner/Off transitions. Existing tracking-hide, no corner fallback, layer ownership and rendering checks remain. The fixture links the real NR/ingress dependencies; no production control stub is used. Test INI/profile are isolated and restored; no owner profile is written.

The Elliottate meter remains a replacement for the corner number, with exactly the existing counters and optional controller follow. FSR, all providers/shaders, the V472 black-bar opt-in corrections, scheduling, telemetry formulas, IPC47101/providerABI45601 and auto-arm startup are retained. No upstream import or new performance calculation. Owner headset confirmation of the repaired selection is pending. No normal tray activation, registry registration, game installation, push or publication.
