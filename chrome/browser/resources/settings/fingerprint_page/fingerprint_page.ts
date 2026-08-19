import 'chrome://resources/cr_elements/cr_button/cr_button.js';
import '../settings_page/settings_section.js';

import {WebUiListenerMixinLit} from 'chrome://resources/cr_elements/web_ui_listener_mixin_lit.js';
import {CrLitElement} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import {getSearchManager} from '../search_settings.js';
import type {SettingsPlugin} from '../settings_main/settings_plugin.js';

import type {FingerprintBrowserProxy, FingerprintRow} from './fingerprint_browser_proxy.js';
import {FingerprintBrowserProxyImpl} from './fingerprint_browser_proxy.js';
import {getCss} from './fingerprint_page.css.js';
import {getHtml} from './fingerprint_page.html.js';

const SettingsFingerprintPageElementBase =
    WebUiListenerMixinLit(CrLitElement);

export class SettingsFingerprintPageElement extends
    SettingsFingerprintPageElementBase implements SettingsPlugin {
  static get is() {
    return 'settings-fingerprint-page';
  }

  static override get styles() {
    return getCss();
  }

  override render() {
    return getHtml.bind(this)();
  }

  static override get properties() {
    return {
      rows_: {type: Array},
    };
  }

  protected accessor rows_: FingerprintRow[] = [];

  private browserProxy_: FingerprintBrowserProxy =
      FingerprintBrowserProxyImpl.getInstance();

  override firstUpdated() {
    this.addWebUiListener(
        'fingerprint-rows-changed', (rows: FingerprintRow[]) => {
          this.rows_ = rows;
        });
    this.browserProxy_.initializeFingerprint();
  }

  protected onNewTabClick_(e: Event) {
    const id = (e.currentTarget as HTMLElement).dataset['id']!;
    this.browserProxy_.openFingerprintTab(id);
  }

  async searchContents(query: string) {
    const searchRequest = await getSearchManager().search(query, this);
    return searchRequest.getSearchResult();
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-fingerprint-page': SettingsFingerprintPageElement;
  }
}

customElements.define(
    SettingsFingerprintPageElement.is, SettingsFingerprintPageElement);
