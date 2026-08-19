import {html} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {SettingsFingerprintPageElement} from './fingerprint_page.js';

export function getHtml(this: SettingsFingerprintPageElement) {
  return html`<!--_html_template_start_-->
<settings-section page-title="$i18n{fingerprintPageTitle}"
    class="cr-centered-card-container">
  ${this.rows_.map(row => html`
    <div class="cr-row">
      <div class="flex">${row.label}</div>
      <div class="separator"></div>
      <cr-button data-id="${row.id}" @click="${this.onNewTabClick_}">
        $i18n{fingerprintNewTab}
      </cr-button>
    </div>
  `)}
</settings-section>
<!--_html_template_end_-->`;
}
