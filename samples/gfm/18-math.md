# Math

GitHub renders `$…$`, `$$…$$` and ` ```math ` blocks with MathJax. mdview typesets them itself with
the OpenType MATH table of Cambria Math: real fractions, roots, limits, stretchy delimiters,
matrices and accents. Copying or searching still sees the TeX source.

## Inline

The mass–energy relation $E = mc^2$, a fraction $\frac{a_1 + a_2}{b_1 \cdot b_2}$, roots
$\sqrt{x^2 + y^2}$ and $\sqrt[3]{2}$, Greek $\alpha, \beta, \Gamma, \Omega$, scripts $x_i^2$ and
$e^{i\pi} + 1 = 0$, a sum $\sum_{k=1}^n k$ and an integral $\int_0^1 f(x)\,dx$ in a sentence: a line
only grows when a formula is taller than the text.

Sets and logic: $A \cup B \subseteq \mathbb{R}$, $\forall \varepsilon > 0\ \exists \delta > 0$,
$f\colon X \to Y$, $\neg (p \land q) \iff \neg p \lor \neg q$, $a \not\in B$, $x \neq y$,
$\lVert v \rVert_2$, $\lfloor x \rfloor$, $\langle u, v \rangle$, $n \bmod 2$, $a \equiv b \pmod{m}$.

With backtick delimiters: $`\sqrt{x^2 + y^2}`$

## Display

$$\sum_{i=1}^n i = \frac{n(n+1)}{2}$$

$$\int_{-\infty}^{\infty} e^{-x^2}\,dx = \sqrt{\pi} \qquad \oint_C \mathbf{F} \cdot d\mathbf{r} = \iint_S (\nabla \times \mathbf{F}) \cdot d\mathbf{S}$$

$$\lim_{x \to 0} \frac{\sin x}{x} = 1 \qquad \prod_{p\ \text{prime}} \frac{1}{1 - p^{-s}} = \zeta(s)$$

$$x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}$$

$$f(x) = \left( \frac{1}{1 + e^{-x}} \right)^{2} \quad \left[ \int_a^b g(t)\,dt \right] \quad \left\{ x \in \mathbb{N} \;\middle|\; x > 2 \right\}$$

$$\binom{n}{k} = \frac{n!}{k!\,(n-k)!} \qquad \big( \Big( \bigg( \Bigg( x \Bigg) \bigg) \Big) \big)$$

$$\mathbf{A} = \begin{pmatrix} a_{11} & a_{12} & \cdots & a_{1n} \\ a_{21} & a_{22} & \cdots & a_{2n} \\ \vdots & \vdots & \ddots & \vdots \\ a_{m1} & a_{m2} & \cdots & a_{mn} \end{pmatrix} \qquad \begin{vmatrix} a & b \\ c & d \end{vmatrix} = ad - bc \qquad \begin{bmatrix} 1 & 0 \\ 0 & 1 \end{bmatrix}$$

$$|x| = \begin{cases} x & \text{if } x \ge 0, \\ -x & \text{otherwise.} \end{cases}$$

$$
\begin{aligned}
\nabla \cdot \vec{E} &= \frac{\rho}{\varepsilon_0} \\
\nabla \times \vec{B} - \frac{1}{c^2}\frac{\partial \vec{E}}{\partial t} &= \mu_0 \vec{J}
\end{aligned}
$$

$$\hat{x} \quad \bar{y} \quad \tilde{z} \quad \dot{a} \quad \ddot{b} \quad \vec{v} \quad \widehat{xyz} \quad \widetilde{abc} \quad \overline{a + b} \quad \underline{c} \quad \overrightarrow{AB}$$

$$\overbrace{1 + 2 + \cdots + n}^{n\ \text{terms}} \qquad \underbrace{a + b + c}_{3} \qquad \overset{\text{def}}{=} \qquad x \xrightarrow{f} y$$

$$\mathbb{N} \subset \mathbb{Z} \subset \mathbb{Q} \subset \mathbb{R} \subset \mathbb{C} \quad \mathcal{L}\{f\}(s) \quad \mathfrak{g} \quad \mathsf{sans} \quad \mathtt{mono} \quad \boldsymbol{\mu} \quad \mathrm{d}x$$

$$e = \sum_{n=0}^{\infty} \frac{1}{n!} = 1 + \cfrac{1}{1 + \cfrac{1}{2 + \cfrac{1}{1 + \cdots}}}$$

$$\textcolor{red}{\text{red}} \quad \color{#0969da} x^2 + y^2 \quad \boxed{E = mc^2}$$

A `math` block:

```math
\mathcal{F}\{f\}(\xi) = \int_{-\infty}^{\infty} f(x)\, e^{-2\pi i x \xi}\,dx
```

Unknown commands stay visible instead of breaking the formula: $\foo{x} + \unknowncommand$.
