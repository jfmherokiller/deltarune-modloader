// Lexical GML mode only; semantic analysis belongs to the future GMEdit adapter.
ace.define('ace/mode/gml', ['require','exports','module','ace/lib/oop','ace/mode/text','ace/mode/text_highlight_rules'], function(require,exports) {
  const oop=require('ace/lib/oop');
  const TextMode=require('ace/mode/text').Mode;
  const TextRules=require('ace/mode/text_highlight_rules').TextHighlightRules;
  function Rules() {
    const keywords=this.createKeywordMapper({
      'keyword':'var|globalvar|static|function|constructor|enum|if|then|else|for|while|repeat|do|until|with|switch|case|default|break|continue|return|exit|try|catch|finally|throw|new|delete|begin|end',
      'keyword.operator':'and|or|not|xor|div|mod',
      'constant.language':'true|false|undefined|noone|all|pi|NaN|infinity',
      'variable.language':'self|other|global'
    },'identifier');
    this.$rules={
      start:[
        {token:'comment',regex:'//.*$'},
        {token:'comment',regex:'/\\*',next:'comment'},
        {token:'keyword',regex:'^\\s*#(?:macro|region|endregion)\\b'},
        {token:'string',regex:'@"',next:'verbatimDouble'},
        {token:'string',regex:"@'",next:'verbatimSingle'},
        {token:'string',regex:'"',next:'double'},
        {token:'string',regex:"'",next:'single'},
        {token:'constant.numeric',regex:'(?:0[xX][0-9a-fA-F]+|\\$[0-9a-fA-F]+|(?:[0-9]+\\.?[0-9]*|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)\\b'},
        {token:keywords,regex:'[a-zA-Z_][a-zA-Z0-9_]*\\b'},
        {token:'keyword.operator',regex:'[-+*/%=<>!&|^~?:]+'},
        {token:'paren.lparen',regex:'[({\\[]'},
        {token:'paren.rparen',regex:'[)}\\]]'},
        {token:'text',regex:'\\s+'}
      ],
      comment:[{token:'comment',regex:'\\*/',next:'start'},{defaultToken:'comment'}],
      double:[{token:'constant.language.escape',regex:'\\\\.'},{token:'string',regex:'"',next:'start'},{defaultToken:'string'}],
      single:[{token:'constant.language.escape',regex:'\\\\.'},{token:'string',regex:"'",next:'start'},{defaultToken:'string'}],
      verbatimDouble:[{token:'string',regex:'"',next:'start'},{defaultToken:'string'}],
      verbatimSingle:[{token:'string',regex:"'",next:'start'},{defaultToken:'string'}]
    };
    this.normalizeRules();
  }
  oop.inherits(Rules,TextRules);
  function Mode() { this.HighlightRules=Rules; this.$id='ace/mode/gml'; }
  oop.inherits(Mode,TextMode);
  Mode.prototype.lineCommentStart='//';
  Mode.prototype.blockComment={start:'/*',end:'*/'};
  exports.Mode=Mode;
});
