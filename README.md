# Tiny Renderer

Unreal Engine で利用可能な、単一の StaticMesh を RenderTarget に描画することだけを考えた小さくて軽量なレンダラプラグインです。

UE 標準の描画パスを利用せずに、単一の StaticMesh を描画するための独自の描画パスを提供します。
Widget 上に 3D モデルを表示したいときなどに利用すると便利です。

## 検証環境
- UE 5.3.2
- Win64

## 使い方
[この記事](https://strv.dev/blog/unrealengine--lets-implement-a-single-mesh-renderer-3/#%E3%83%97%E3%83%A9%E3%82%B0%E3%82%A4%E3%83%B3%E3%81%AE%E5%85%AC%E9%96%8B) を参照してください。

## パフォーマンス
約2万ポリゴンの StaticMesh を同一フレーム内で描画し、手法別の処理時間を比較すると以下のようになります。

| 描画手法 | 処理時間 | フレーム内における割合 |
| --- | --- | --- |
| TinyRenderer | 0.02 ms (16.38 μs) | 0.1 % |
| SceneCapture2D | 4.12 ms | 17.4 % |
| FPreviewScene | 1.11 ms | 4.7 % |

TinyRenderer は一つのメッシュを描画するために必要ないことはしないため、1つのパスで描画が完了し、非常に軽量です。

## サポートしている機能
- Opaque なマテリアルが適用された StaticMesh を RenderTarget に描画

## サポートしない機能
- 多数のメッシュからなるシーンの描画
- 複雑な照明環境の描画
- エフェクトやポストプロセスの適用
- Nanite メッシュの描画 (Fallback Mesh の LOD を指定して描画することは可能)

## 注意事項
このプラグインは、本当に単一の StaticMesh を描画することだけを目的としています。複数のメッシュを描画する場合は、SceneCapture2D や FPreviewScene による実装を利用してください。

また、このプラグインは、描画対象の StaticMesh が Opaque なマテリアルを適用していることを前提としています。透過マテリアルやマテリアルのブレンドモードが適用されている場合は、正しく描画されません。

レンダラ自体が標準とは異なるものであるため、見た目も(よく似ていますが)通常のViewportと完全に同一にはなりません。用途に合わせた使い分けをおすすめします。