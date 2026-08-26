# Unicode conformance data

`NormalizationTest.txt` は Unicode Consortium が配布する正規化の適合性テスト
(Unicode 15.0.0, https://www.unicode.org/Public/15.0.0/ucd/NormalizationTest.txt)。

このライブラリはトークナイザを 2 つ持つ (csrc の C と driver/modernc の Go)。実装の
出所が違う以上、両者が同じ規則に従うことは実装同士の突き合わせではなく、仕様への適合と
して担保する。C は utf8proc、Go は golang.org/x/text を使っており、どちらもこの
テストを通ることを確認したうえで採用している。

版を上げるときは、C 側が使う utf8proc と Go 側が使う x/text の対応する Unicode の版に
合わせる。
